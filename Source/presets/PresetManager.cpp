#include "PresetManager.h"
#include "BinaryData.h"
#include "../state/SoundState.h"
#include "../fx/FxChain.h"

namespace torden
{
    namespace
    {
        const juce::Identifier presetNameProp { "presetName" };
        const juce::Identifier presetCategoryProp { "presetCategory" };
        const juce::Identifier macroNamesProp { "macroNames" };

        int categoryRank (const juce::String& c)
        {
            const int i = PresetManager::categoryOrder.indexOf (c);
            return i < 0 ? PresetManager::categoryOrder.size() : i;
        }
    }

    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um) : state (s), undo (um)
    {
        loadFavorites();
        rescan();
    }

    void PresetManager::loadFavorites()
    {
        favorites.clear();
        if (ignoreUserPresets) return;
        favorites.addLines (getUserPresetFolder().getChildFile ("favorites.txt").loadFileAsString());
        favorites.removeEmptyStrings();
    }

    void PresetManager::saveFavorites() const
    {
        auto folder = getUserPresetFolder();
        if (folder.createDirectory())
            folder.getChildFile ("favorites.txt").replaceWithText (favorites.joinIntoString ("\n"));
    }

    bool PresetManager::isFavorite (int index) const
    {
        return juce::isPositiveAndBelow (index, (int) presets.size())
            && favorites.contains (favoriteKey (presets[(size_t) index]));
    }

    void PresetManager::toggleFavorite (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()))
            return;

        const auto key = favoriteKey (presets[(size_t) index]);
        if (favorites.contains (key)) favorites.removeString (key);
        else                          favorites.add (key);
        saveFavorites();
        sendChangeMessage();
    }

    bool PresetManager::deleteUserPreset (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()) || presets[(size_t) index].isFactory)
            return false;

        const auto key = favoriteKey (presets[(size_t) index]);
        if (! presets[(size_t) index].file.moveToTrash())
            return false;

        if (favorites.contains (key))
        {
            favorites.removeString (key);
            saveFavorites();
        }
        rescan();
        return true;
    }

    juce::File PresetManager::getUserPresetFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Fjord/Torden/Presets");
    }

    bool PresetManager::parse (const juce::String& xmlText, Preset& out)
    {
        // Quick scan: read only the root element (name, category). The parameters are parsed on load.
        const int start = xmlText.indexOf ("<TordenPreset");
        const int end = start >= 0 ? xmlText.indexOfChar (start, '>') : -1;
        if (start < 0 || end < 0)
            return false;

        auto header = xmlText.substring (start, end);
        if (header.endsWithChar ('/')) header = header.dropLastCharacters (1);
        auto xml = juce::parseXML (header + "/>");
        if (xml == nullptr || ! xml->hasTagName ("TordenPreset"))
            return false;

        out.name = xml->getStringAttribute ("name", "Untitled");
        out.category = xml->getStringAttribute ("category", "User");
        out.author = xml->getStringAttribute ("author");
        out.tags = juce::StringArray::fromTokens (xml->getStringAttribute ("tags"), ",", "");
        out.tags.trim();
        out.tags.removeEmptyStrings();
        out.xml = xmlText;
        return true;
    }

    void PresetManager::rescan()
    {
        const auto currentName = getCurrentName();
        presets.clear();

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const juce::String file (BinaryData::originalFilenames[i]);
            if (! file.endsWithIgnoreCase (".torden"))
                continue;

            int size = 0;
            if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
            {
                Preset p;
                if (parse (juce::String::fromUTF8 (data, size), p))
                    presets.push_back (std::move (p));
            }
        }

        for (const auto& f : getUserPresetFolder().findChildFiles (juce::File::findFiles, true, "*.torden"))
        {
            if (ignoreUserPresets) break;
            Preset p;
            if (parse (f.loadFileAsString(), p))
            {
                p.file = f;
                p.isFactory = false;
                presets.push_back (std::move (p));
            }
        }

        std::stable_sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b)
        {
            if (a.isFactory != b.isFactory) return a.isFactory;
            const int ra = categoryRank (a.category), rb = categoryRank (b.category);
            if (ra != rb) return ra < rb;
            return a.name.compareNatural (b.name) < 0;
        });

        currentIndex = -1;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == currentName)
                currentIndex = (int) i;

        sendChangeMessage();
    }

    juce::String PresetManager::getCurrentName() const
    {
        return state.state.getProperty (presetNameProp, "Init").toString();
    }

    juce::String PresetManager::getCurrentCategory() const
    {
        return state.state.getProperty (presetCategoryProp, "").toString();
    }

    void PresetManager::setCurrent (int index, const juce::String& name, const juce::String& category)
    {
        currentIndex = index;
        state.state.setProperty (presetNameProp, name, nullptr);
        state.state.setProperty (presetCategoryProp, category, nullptr);
        sendChangeMessage();
    }

    void PresetManager::applyXml (const juce::XmlElement& xml, bool forcePattern)
    {
        std::map<juce::String, float> values;
        for (auto* e : xml.getChildWithTagNameIterator ("Param"))
            values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");

        if (undo != nullptr) undo->beginNewTransaction ("Load preset");

        for (auto* p : state.processor.getParameters())
        {
            auto* param = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (param == nullptr)
                continue;
            const auto it = values.find (param->getParameterID());
            const float normalised = it != values.end() ? param->convertTo0to1 (it->second) : param->getDefaultValue();
            param->beginChangeGesture();
            param->setValueNotifyingHost (normalised);
            param->endChangeGesture();
        }

        // Envelopes: missing ones fall back to the default shape
        auto envs = state.state.getOrCreateChildWithName (SoundState::envelopesId, nullptr);
        const auto* xmlEnvs = xml.getChildByName ("Envelopes");
        for (int i = 0; i < dsp::kNumEnvs; ++i)
        {
            const auto e = (dsp::Env) i;
            juce::String points = dsp::defaultEnv (e).toString();
            if (xmlEnvs != nullptr)
                for (auto* env : xmlEnvs->getChildWithTagNameIterator ("Env"))
                    if (env->getStringAttribute ("id") == dsp::envName (e))
                        points = dsp::EnvShape::fromString (env->getStringAttribute ("points"), dsp::defaultEnv (e)).toString();
            auto node = envs.getChildWithProperty (SoundState::idProp, dsp::envName (e));
            if (node.isValid())
                node.setProperty (SoundState::pointsProp, points, undo);
        }

        if (forcePattern || getLoadPatterns())
        {
            const auto* pattern = xml.getChildByName ("Pattern");
            const auto steps = pattern != nullptr ? pattern->getStringAttribute ("steps")
                                                  : seq::Pattern::fourOnTheFloor().toString();
            state.state.getOrCreateChildWithName (SoundState::patternId, nullptr).setProperty (SoundState::stepsProp, steps, undo);
        }

        state.state.setProperty (macroNamesProp, xml.getStringAttribute ("macros"), undo);
        state.state.setProperty ("fxOrder", fx::orderToString (fx::orderFromString (xml.getStringAttribute ("fxOrder"))), undo);
    }

    std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml (const juce::String& name, const juce::String& category,
                                                                       const juce::StringArray& tags) const
    {
        auto xml = std::make_unique<juce::XmlElement> ("TordenPreset");
        xml->setAttribute ("name", name);
        xml->setAttribute ("category", category);
        xml->setAttribute ("author", "User");
        if (! tags.isEmpty())
            xml->setAttribute ("tags", tags.joinIntoString (","));
        xml->setAttribute ("macros", state.state.getProperty (macroNamesProp).toString());
        xml->setAttribute ("fxOrder", state.state.getProperty ("fxOrder").toString());

        for (auto* p : state.processor.getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* e = xml->createNewChildElement ("Param");
                e->setAttribute ("id", param->getParameterID());
                e->setAttribute ("value", param->convertFrom0to1 (param->getValue()));
            }

        auto* envs = xml->createNewChildElement ("Envelopes");
        const auto envTree = state.state.getChildWithName (SoundState::envelopesId);
        for (int i = 0; i < dsp::kNumEnvs; ++i)
        {
            const auto e = (dsp::Env) i;
            auto* env = envs->createNewChildElement ("Env");
            env->setAttribute ("id", dsp::envName (e));
            env->setAttribute ("points", envTree.getChildWithProperty (SoundState::idProp, dsp::envName (e))
                                                .getProperty (SoundState::pointsProp, dsp::defaultEnv (e).toString()).toString());
        }
        auto* pattern = xml->createNewChildElement ("Pattern");
        pattern->setAttribute ("steps", state.state.getChildWithName (SoundState::patternId).getProperty (SoundState::stepsProp).toString());
        return xml;
    }

    void PresetManager::load (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()))
            return;

        const auto& p = presets[(size_t) index];
        if (auto xml = juce::parseXML (p.xml))
        {
            applyXml (*xml);
            setCurrent (index, p.name, p.category);
        }
    }

    void PresetManager::loadInit()
    {
        applyXml (juce::XmlElement ("TordenPreset"), true);
        setCurrent (-1, "Init", "");
    }

    void PresetManager::step (int delta)
    {
        if (presets.empty())
            return;

        const int n = (int) presets.size();
        const int start = currentIndex < 0 ? (delta > 0 ? -1 : 0) : currentIndex;
        load (((start + delta) % n + n) % n);
    }

    juce::StringArray PresetManager::getCurrentTags() const
    {
        return juce::isPositiveAndBelow (currentIndex, (int) presets.size()) ? presets[(size_t) currentIndex].tags
                                                                             : juce::StringArray();
    }

    bool PresetManager::saveUserPreset (const juce::String& name, const juce::String& category, const juce::StringArray& tags)
    {
        const auto cleanName = name.trim().isEmpty() ? juce::String ("Untitled") : name.trim();
        auto xmlPtr = createPresetXml (cleanName, category, tags);
        auto& xml = *xmlPtr;

        auto folder = getUserPresetFolder().getChildFile (category);
        if (! folder.createDirectory())
            return false;

        auto file = folder.getChildFile (juce::File::createLegalFileName (cleanName) + ".torden");
        if (! xml.writeTo (file))
            return false;

        state.state.setProperty (presetNameProp, cleanName, nullptr);
        rescan();
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].file == file)
                setCurrent ((int) i, cleanName, category);
        return true;
    }

    void PresetManager::restoreFromState()
    {
        const auto name = getCurrentName();
        currentIndex = -1;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == name)
                currentIndex = (int) i;
        sendChangeMessage();
    }
}
