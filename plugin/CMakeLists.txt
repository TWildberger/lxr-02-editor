# JUCE 8 (AGPLv3 / commercial). KitForge is GPL-3.0-or-later, which is compatible with the AGPLv3 option.
# Offline builds: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE-8.0.15
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.15.tar.gz
    URL_HASH SHA256=0eafd736286c6e7bb6958d79fce18d42cb654b8e231890d327b7cece379bd156)
FetchContent_MakeAvailable(JUCE)

if(KITFORGE_BUILD_PLUGIN)
juce_add_plugin(KitForge
    COMPANY_NAME "TWildberger"
    COMPANY_WEBSITE "https://github.com/TWildberger/lxr-02-editor"
    BUNDLE_ID "com.twildberger.kitforge"
    PLUGIN_MANUFACTURER_CODE Twld
    PLUGIN_CODE Kfg2
    PRODUCT_NAME "KitForge"
    DESCRIPTION "Editor and librarian for LXR-02"
    FORMATS ${COMPANION_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Tools
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(KitForge PRIVATE
    Source/PluginProcessor.cpp
    Source/DeviceSession.cpp
    Source/PluginEditor.cpp
    Source/ParamControl.cpp
    Source/VoicePage.cpp
    Source/ModMatrixPage.cpp
    Source/GlobalsPage.cpp
    Source/LibraryPage.cpp
    Source/DevicePage.cpp
    Source/LookAndFeel.cpp)

target_compile_definitions(KitForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(KitForge
    PRIVATE
        kitforge::core
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_data_structures
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless processor/editor checks (ctest: plugin_smoke). Renders every page to PNG when given a folder.
if(KITFORGE_BUILD_TESTS)
    juce_add_console_app(KitForgeSmokeTest PRODUCT_NAME "KitForgeSmokeTest")
    target_sources(KitForgeSmokeTest PRIVATE
        SmokeTest/Main.cpp
        Source/PluginProcessor.cpp
        Source/DeviceSession.cpp
        Source/PluginEditor.cpp
        Source/ParamControl.cpp
        Source/VoicePage.cpp
        Source/ModMatrixPage.cpp
        Source/GlobalsPage.cpp
        Source/LibraryPage.cpp
        Source/DevicePage.cpp
        Source/LookAndFeel.cpp)
    target_compile_definitions(KitForgeSmokeTest PRIVATE
        JucePlugin_Name="KitForge"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(KitForgeSmokeTest PRIVATE
        kitforge::core
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_data_structures
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME plugin_smoke COMMAND KitForgeSmokeTest)
endif()
endif()

if(VSYNTHXT_BUILD_PLUGIN)
    add_subdirectory(vsynthxt)
endif()
