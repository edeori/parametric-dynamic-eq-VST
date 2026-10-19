cmake_minimum_required(VERSION 3.22)

project(ParaDynEQ VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PARADYNEQ_ENABLE_LICENSING "Build ParaDyn EQ with the reusable licensing module enabled" ON)
set(PARADYNEQ_LICENSE_SERVER_BASE_URL "" CACHE STRING "Optional build-time override for the licensing server base URL")
set(PARADYNEQ_LICENSE_PRODUCT_ID "" CACHE STRING "Optional build-time override for the licensing product ID")
set(PARADYNEQ_LICENSE_PUBLIC_KEY "" CACHE STRING "Optional build-time override for the licensing public key")

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

set(PARADYNEQ_ASSETS
    Assets/background.png
    Assets/knob.png
    Assets/moth_logo.png
    Assets/Fonts/Oxanium-Bold.ttf
    Assets/Fonts/BarlowCondensed-Light.ttf
    Assets/Fonts/BarlowCondensed-Medium.ttf
    Assets/Fonts/Rajdhani-Medium.ttf
)

if (PARADYNEQ_ENABLE_LICENSING)
    list(APPEND PARADYNEQ_ASSETS Assets/licensing_config.json)
endif()

juce_add_binary_data(ParaDynEQAssets SOURCES ${PARADYNEQ_ASSETS})

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

if (APPLE AND PARADYNEQ_ENABLE_LICENSING)
    set_target_properties(ParaDynEQ PROPERTIES
        JUCE_PLIST_TO_MERGE
            "<key>NSAppTransportSecurity</key><dict><key>NSAllowsArbitraryLoads</key><true/></dict>")
endif()

# Lay artefacts out as build/<FORMAT>/... instead of the JUCE default of
# build/ParaDynEQ_artefacts/<CONFIG>/<FORMAT>/..., so each format's build result
# lands directly under a top-level Standalone/VST3/AU folder.
set_target_properties(ParaDynEQ PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Explicit configuration directory keeps Windows installer inputs deterministic.
if(WIN32)
    set_target_properties(ParaDynEQ PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
endif()

set(PARADYNEQ_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EQBand.cpp
    Source/DynamicDetector.cpp
    Source/Oversampler.cpp
    Source/SpectrumAnalyzer.cpp
    Source/MatchEQ.cpp
    Source/MatchEQPanel.cpp
    Source/BandEditor.cpp
    Source/ParaDynFonts.cpp
    Source/ParaDynLookAndFeel.cpp
)

if (PARADYNEQ_ENABLE_LICENSING)
    list(APPEND PARADYNEQ_SOURCES
        Source/Licensing/LicenseConfig.cpp
        Source/Licensing/LicenseClient.cpp
        Source/Licensing/LicenseManager.cpp
        Source/Licensing/LicenseStore.cpp
        Source/Licensing/MachineFingerprint.cpp
        Source/Licensing/TrialManager.cpp)
endif()

target_sources(ParaDynEQ PRIVATE ${PARADYNEQ_SOURCES})

target_compile_definitions(ParaDynEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    PARADYNEQ_ENABLE_LICENSING=$<BOOL:${PARADYNEQ_ENABLE_LICENSING}>
)

if (PARADYNEQ_ENABLE_LICENSING)
    if (NOT PARADYNEQ_LICENSE_SERVER_BASE_URL STREQUAL "")
        target_compile_definitions(ParaDynEQ PUBLIC LICENSE_SERVER_BASE_URL="${PARADYNEQ_LICENSE_SERVER_BASE_URL}")
    endif()
    if (NOT PARADYNEQ_LICENSE_PRODUCT_ID STREQUAL "")
        target_compile_definitions(ParaDynEQ PUBLIC LICENSE_PRODUCT_ID="${PARADYNEQ_LICENSE_PRODUCT_ID}")
    endif()
    if (NOT PARADYNEQ_LICENSE_PUBLIC_KEY STREQUAL "")
        target_compile_definitions(ParaDynEQ PUBLIC LICENSE_PUBLIC_KEY="${PARADYNEQ_LICENSE_PUBLIC_KEY}")
    endif()
endif()

target_link_libraries(ParaDynEQ
    PRIVATE juce::juce_audio_utils juce::juce_dsp ParaDynEQAssets
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags
)

if (PARADYNEQ_ENABLE_LICENSING)
    target_compile_definitions(ParaDynEQ PUBLIC LICENSE_PRODUCT_VERSION="${PROJECT_VERSION}")
    target_link_libraries(ParaDynEQ PRIVATE juce::juce_cryptography)
    if (APPLE)
        target_link_libraries(ParaDynEQ PRIVATE "-framework IOKit")
    endif()
endif()

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
    juce_add_console_app(ParaDynEQMatchTests PRODUCT_NAME "ParaDyn EQ Match Tests")
    target_sources(ParaDynEQMatchTests PRIVATE Tests/MatchEQTests.cpp Source/MatchEQ.cpp)
    target_link_libraries(ParaDynEQMatchTests PRIVATE juce::juce_dsp
                          PUBLIC juce::juce_recommended_config_flags)
    add_test(NAME ParaDynEQMatch COMMAND ParaDynEQMatchTests)

    # Exercise processor state, host parameters, actual audio and editor layout
    # without reading/writing the developer's licence or contacting a server.
    juce_add_console_app(ParaDynEQMatchIntegrationTests PRODUCT_NAME "ParaDyn EQ Match Integration Tests")
    set(PARADYNEQ_TEST_SOURCES ${PARADYNEQ_SOURCES})
    list(FILTER PARADYNEQ_TEST_SOURCES EXCLUDE REGEX "/Licensing/")
    target_sources(ParaDynEQMatchIntegrationTests PRIVATE Tests/MatchEQIntegrationTests.cpp ${PARADYNEQ_TEST_SOURCES})
    target_compile_definitions(ParaDynEQMatchIntegrationTests PRIVATE
        PARADYNEQ_ENABLE_LICENSING=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="ParaDyn EQ")
    target_link_libraries(ParaDynEQMatchIntegrationTests PRIVATE juce::juce_audio_utils juce::juce_dsp ParaDynEQAssets
                          PUBLIC juce::juce_recommended_config_flags)
    add_test(NAME ParaDynEQMatchIntegration COMMAND ParaDynEQMatchIntegrationTests)
endif()

if(PARADYNEQ_BUILD_TESTS AND PARADYNEQ_ENABLE_LICENSING)
    juce_add_console_app(ParaDynEQFingerprintTests PRODUCT_NAME "ParaDyn EQ Fingerprint Tests")
    target_sources(ParaDynEQFingerprintTests PRIVATE Tests/MachineFingerprintTests.cpp Source/Licensing/MachineFingerprint.cpp)
    target_include_directories(ParaDynEQFingerprintTests PRIVATE Source)
    target_link_libraries(ParaDynEQFingerprintTests PRIVATE juce::juce_cryptography)
    if(APPLE)
        target_link_libraries(ParaDynEQFingerprintTests PRIVATE "-framework IOKit")
    endif()
    add_test(NAME ParaDynEQFingerprint COMMAND ParaDynEQFingerprintTests)
endif()
