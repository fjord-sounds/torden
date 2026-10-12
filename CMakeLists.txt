cmake_minimum_required(VERSION 3.22)
project(Torden VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

# Plugin formats per platform: VST3 + Standalone everywhere, AU on macOS, LV2 on Linux.
set(TORDEN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TORDEN_FORMATS AU)
elseif(UNIX)
    list(APPEND TORDEN_FORMATS LV2)
endif()

# JUCE 8 is not part of this repository. A local checkout in external/JUCE (or -DTORDEN_JUCE_DIR=<path>) is used
# if present; otherwise the pinned release is downloaded when CMake configures.
set(TORDEN_JUCE_TAG "9.0.4")
set(TORDEN_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "Local JUCE checkout (optional)")
if(EXISTS "${TORDEN_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${TORDEN_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                              GIT_TAG ${TORDEN_JUCE_TAG} GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Git commit shown in the About panel (empty outside a git checkout)
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE TORDEN_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

# Copy the built plugins to the user's plugin folders (turn off in CI: -DTORDEN_COPY_PLUGIN=OFF)
option(TORDEN_COPY_PLUGIN "Install the plugins to the user plugin folders after building" ON)

juce_add_plugin(Torden
    COMPANY_NAME "Fjord"
    BUNDLE_ID "com.fjord.torden"
    PLUGIN_MANUFACTURER_CODE Fjrd
    PLUGIN_CODE Trdn
    FORMATS ${TORDEN_FORMATS}
    LV2URI "https://github.com/fjord-sounds/torden"
    PRODUCT_NAME "Torden"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    VERSION ${PROJECT_VERSION}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Drum Synth
    COPY_PLUGIN_AFTER_BUILD ${TORDEN_COPY_PLUGIN})

file(GLOB_RECURSE TORDEN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(Torden PRIVATE ${TORDEN_SOURCES})
target_include_directories(Torden PRIVATE Source)

# Factory presets are embedded in the plugin.
file(GLOB_RECURSE TORDEN_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/Factory/*.torden)
# Font: Manrope (SIL Open Font License, see resources/fonts/OFL.txt)
file(GLOB TORDEN_FONTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/*.ttf)
juce_add_binary_data(TordenBinaryData SOURCES ${TORDEN_PRESETS} ${TORDEN_FONTS})
set_target_properties(TordenBinaryData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(Torden PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
if(TORDEN_GIT_HASH)
    target_compile_definitions(Torden PUBLIC TORDEN_GIT_HASH="${TORDEN_GIT_HASH}")
endif()

target_link_libraries(Torden
    PRIVATE TordenBinaryData juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Offline render test (no DAW): cmake --build build --target TordenRenderTest
juce_add_console_app(TordenRenderTest PRODUCT_NAME "TordenRenderTest")
target_sources(TordenRenderTest PRIVATE tests/RenderTest.cpp ${TORDEN_SOURCES})
target_include_directories(TordenRenderTest PRIVATE Source)
target_compile_definitions(TordenRenderTest PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JucePlugin_Name="Torden" JucePlugin_VersionString="${PROJECT_VERSION}" JucePlugin_Build_Standalone=0)
if(TORDEN_GIT_HASH)
    target_compile_definitions(TordenRenderTest PRIVATE TORDEN_GIT_HASH="${TORDEN_GIT_HASH}")
endif()
target_link_libraries(TordenRenderTest PRIVATE
    TordenBinaryData juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats juce::juce_recommended_config_flags)
if(APPLE)
    set_target_properties(TordenRenderTest PROPERTIES OSX_ARCHITECTURES arm64)
endif()
