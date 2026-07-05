cmake_minimum_required(VERSION 3.22)

# Must be set BEFORE project() so the toolchain picks it up on first configure
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

project(Octane VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(COPY_AFTER_BUILD "Copy plugins to system folders after build" ON)
option(BUILD_DEMO "Build demo version (60s play / 10s mute, no activation)" OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(JUCE)

# WebView UI — single self-contained HTML file, no npm build step.
# Own BinaryData namespace (OctaneWebUI) so it can't collide with other
# binary-data targets at link time (Tonic gotcha #2).
juce_add_binary_data(OctaneWebViewUI
    NAMESPACE OctaneWebUI
    HEADER_NAME OctaneWebUIBinary.h
    SOURCES
        ui/index.html
        JUCE/modules/juce_gui_extra/native/javascript/index.js
        JUCE/modules/juce_gui_extra/native/javascript/check_native_interop.js
)

if(APPLE)
    set(AAX_SDK_PATH "/Users/soda/Documents/aax-sdk-2-9-0")
    if(EXISTS "${AAX_SDK_PATH}")
        set(FORMATS VST3 AU AAX Standalone)
    else()
        message(WARNING "AAX SDK not found at ${AAX_SDK_PATH} — building without AAX")
        set(FORMATS VST3 AU Standalone)
    endif()
elseif(WIN32)
    set(AAX_SDK_PATH "C:/SDKs/aax-sdk-2-9-0")
    if(EXISTS "${AAX_SDK_PATH}")
        set(FORMATS VST3 AAX Standalone)
    else()
        set(FORMATS VST3 Standalone)
    endif()
else()
    set(FORMATS VST3 Standalone)
endif()

set(SOURCE_FILES
    Source/PluginProcessor.cpp
    Source/UI/WebViewEditor.cpp
    Source/Licensing/LicenseManager.cpp
    Source/Licensing/ActivationDialog.cpp
)

juce_add_plugin(Octane
    COMPANY_NAME "Carbonated Audio"
    PLUGIN_MANUFACTURER_CODE CbAu
    PLUGIN_CODE Octn
    FORMATS ${FORMATS}
    PRODUCT_NAME "FIZZFUEL"
    BUNDLE_ID "com.carbonatedaudio.octane"

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    NEEDS_WEBVIEW2 TRUE

    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD}

    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Harmonic
)

target_sources(Octane PRIVATE ${SOURCE_FILES})

target_compile_definitions(Octane
    PUBLIC
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
)

if(BUILD_DEMO)
    target_compile_definitions(Octane PRIVATE DEMO_BUILD=1)
endif()

if(APPLE AND EXISTS "${AAX_SDK_PATH}")
    target_compile_definitions(Octane PUBLIC
        JucePlugin_AAXLibs_path="${AAX_SDK_PATH}/Libs"
    )
endif()

target_link_libraries(Octane
    PRIVATE
        OctaneWebViewUI
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(Octane
    PRIVATE
        Source
)

option(BUILD_IDLE_TEST "Build the idle-noise measurement harness" OFF)
if(BUILD_IDLE_TEST)
    juce_add_console_app(idle_test PRODUCT_NAME "idle_test")
    target_sources(idle_test PRIVATE Source/test_idle.cpp)
    target_compile_definitions(idle_test PUBLIC JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_include_directories(idle_test PRIVATE Source)
    target_link_libraries(idle_test PRIVATE
        juce::juce_dsp juce::juce_audio_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

if(MSVC)
    target_compile_options(Octane PRIVATE /W4)
else()
    target_compile_options(Octane PRIVATE -Wall -Wextra -Wpedantic)
endif()
