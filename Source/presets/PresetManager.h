#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace torden
{
    // Presets are XML files (.torden) with plain values:
    //   <TordenPreset name="Deep Stone" category="Techno" author="Fjord"><Param id="body_level" value="0.8"/>…
    // Missing parameters get their default value – this keeps old presets compatible with new versions.
    class PresetManager : public juce::ChangeBroadcaster
    {
    public:
        struct Preset
        {
            juce::String name, category, author;
            juce::StringArray tags;
            juce::String xml;          // file contents
            juce::File file;           // empty for factory presets
            bool isFactory = true;
        };

        // Tag vocabulary in display order (must match the tags used by the factory presets)
        static inline const juce::StringArray tagOrder {
            "Punchy", "Deep", "Long", "Short", "Tonal", "Clicky", "Distorted", "Clean", "Roomy", "Noisy",
            "Metallic", "Pitched", "Sub", "Analog", "Lo-Fi", "Wide", "Pattern" };

        static inline const juce::StringArray categoryOrder { "Techno", "House", "Hardstyle", "Trap", "Hip-Hop",
                                                               "DnB", "Industrial", "Minimal", "Acoustic", "Cinematic", "Pop",
                                                               "Tropical", "User" };

        // Screenshots: show only the factory presets, never the user's own presets or favourites.
        static inline bool ignoreUserPresets = false;

        PresetManager (juce::AudioProcessorValueTreeState&, juce::UndoManager* = nullptr);

        void rescan();
        const std::vector<Preset>& getPresets() const { return presets; }

        int getCurrentIndex() const { return currentIndex; }
        juce::String getCurrentName() const;
        juce::String getCurrentCategory() const;

        void load (int index);
        void loadInit();
        void next()     { step (1); }
        void previous() { step (-1); }

        // Saves the current sound as a user preset and selects it.
        bool saveUserPreset (const juce::String& name, const juce::String& category, const juce::StringArray& tags = {});

        // Tags of the active preset (empty for Init)
        juce::StringArray getCurrentTags() const;

        static juce::File getUserPresetFolder();

        // Favorites (★), stored in favorites.txt in the user folder
        bool isFavorite (int index) const;
        void toggleFavorite (int index);

        // Only user presets can be deleted.
        bool deleteUserPreset (int index);

        juce::ValueTree& getState() { return state.state; }

        // When off, loading a preset keeps the current step pattern (stored as "loadPatterns" in the state).
        bool getLoadPatterns() const { return state.state.getProperty ("loadPatterns", true); }
        void setLoadPatterns (bool shouldLoad) { state.state.setProperty ("loadPatterns", shouldLoad, nullptr); }

        // Writes the current sound as preset XML (also used for A/B snapshots and the WAV export name).
        std::unique_ptr<juce::XmlElement> createPresetXml (const juce::String& name, const juce::String& category,
                                                           const juce::StringArray& tags = {}) const;
        // Applies preset XML (parameters, envelopes, effect order, macro names, optional pattern).
        void applyXml (const juce::XmlElement&, bool forcePattern = false);

        // For host recall: keep the name of the active preset in the state.
        void restoreFromState();

    private:
        void step (int delta);
        void setCurrent (int index, const juce::String& name, const juce::String& category);
        static bool parse (const juce::String& xmlText, Preset& out);
        static juce::String favoriteKey (const Preset& p) { return (p.isFactory ? "factory/" : "user/") + p.category + "/" + p.name; }
        void loadFavorites();
        void saveFavorites() const;

        juce::AudioProcessorValueTreeState& state;
        juce::UndoManager* undo = nullptr;
        std::vector<Preset> presets;
        int currentIndex = -1;
        juce::StringArray favorites;
    };
}
