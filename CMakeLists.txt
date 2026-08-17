cmake_minimum_required(VERSION 3.22)

project(ParaDynEQ VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS target" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ParaDynEQ
    COMPANY_NAME "Moth Production"
    BUNDLE_ID "com.mothproduction.paradyneq"
    PLUGIN_MANUFACTURER_CODE MthP
    PLUGIN_CODE Pdeq
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "ParaDyn EQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ
    AU_MAIN_TYPE kAudioUnitType_Effect
)

# Lay artefacts out as build/<FORMAT>/... instead of the JUCE default of
# build/ParaDynEQ_artefacts/<CONFIG>/<FORMAT>/..., so each format's build result
# lands directly under a top-level Standalone/VST3/AU folder.
set_target_properties(ParaDynEQ PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_sources(ParaDynEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EQBand.cpp
    Source/DynamicDetector.cpp
    Source/Oversampler.cpp
    Source/SpectrumAnalyzer.cpp
    Source/BandEditor.cpp
)

target_compile_definitions(ParaDynEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(ParaDynEQ
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags
)

option(PARADYNEQ_BUILD_TESTS "Build DSP unit tests" ON)
if(PARADYNEQ_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(ParaDynEQTests PRODUCT_NAME "ParaDyn EQ Tests")
    target_sources(ParaDynEQTests PRIVATE
        Tests/DspTests.cpp
        Source/DynamicDetector.cpp
        Source/EQBand.cpp
    )
    target_link_libraries(ParaDynEQTests PRIVATE juce::juce_dsp juce::juce_audio_basics
                          PUBLIC juce::juce_recommended_config_flags)
    add_test(NAME ParaDynEQDSP COMMAND ParaDynEQTests)
endif()
