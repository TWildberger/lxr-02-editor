juce_add_plugin(VSynthXT
    COMPANY_NAME "TWildberger"
    BUNDLE_ID "com.twildberger.vsynthxt"
    PLUGIN_MANUFACTURER_CODE Twld
    PLUGIN_CODE Vsxt
    PRODUCT_NAME "V-Synth XT Companion"
    DESCRIPTION "Independent hardware editor and librarian for Roland V-Synth XT"
    FORMATS ${COMPANION_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Tools
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(XT_SOURCES Session.cpp Processor.cpp Editor.cpp NativePage.cpp)
target_sources(VSynthXT PRIVATE ${XT_SOURCES})
target_compile_definitions(VSynthXT PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_REPORT_APP_USAGE=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
target_link_libraries(VSynthXT PRIVATE vsynthxt_core juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(APPLE)
    foreach(format VST3 AU Standalone)
        add_custom_command(TARGET VSynthXT_${format} POST_BUILD
            COMMAND /usr/bin/codesign --force --deep --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:VSynthXT_${format}>"
            VERBATIM)
    endforeach()
endif()

if(VSYNTHXT_BUILD_TESTS)
    juce_add_console_app(VSynthXTSmokeTest PRODUCT_NAME "VSynthXTSmokeTest")
    target_sources(VSynthXTSmokeTest PRIVATE SmokeTest.cpp ${XT_SOURCES})
    target_compile_definitions(VSynthXTSmokeTest PRIVATE
        JucePlugin_Name="V-Synth XT Companion" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_REPORT_APP_USAGE=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(VSynthXTSmokeTest PRIVATE vsynthxt_core juce::juce_audio_utils)
    add_test(NAME vsynthxt_smoke COMMAND VSynthXTSmokeTest)
    set_tests_properties(vsynthxt_smoke PROPERTIES
        ENVIRONMENT "VSYNTHXT_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/smoke-library.sqlite")

    juce_add_console_app(VSynthXTVST3Test PRODUCT_NAME "VSynthXTVST3Test")
    target_sources(VSynthXTVST3Test PRIVATE VST3Test.cpp)
    target_compile_definitions(VSynthXTVST3Test PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_REPORT_APP_USAGE=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(VSynthXTVST3Test PRIVATE vsynthxt_core juce::juce_audio_utils)
    add_dependencies(VSynthXTVST3Test VSynthXT_VST3)
    get_target_property(XT_VST3_ARTEFACT VSynthXT_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
    add_test(NAME vsynthxt_vst3_host COMMAND VSynthXTVST3Test "${XT_VST3_ARTEFACT}")
    set_tests_properties(vsynthxt_vst3_host PROPERTIES
        TIMEOUT 60
        ENVIRONMENT "VSYNTHXT_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/vst3-test-library.sqlite")
endif()
