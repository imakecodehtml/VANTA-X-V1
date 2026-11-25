cmake_minimum_required(VERSION 3.22)
project(VantaX VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(VantaX
    PRODUCT_NAME "VANTA X"
    COMPANY_NAME "VANTA"
    PLUGIN_MANUFACTURER_CODE Vnta
    PLUGIN_CODE Vtax
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

target_sources(VantaX PRIVATE
    Source/PluginProcessor.cpp
    Source/Presets/PresetManager.cpp
    Source/Audio/SampleBank.cpp
    Source/UI/PluginEditor.cpp)

target_include_directories(VantaX PRIVATE Source)

target_compile_definitions(VantaX PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(VantaX
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Plain C++ DSP unit test (no JUCE dependency)
add_executable(VantaDspTest Tests/dsp_test.cpp)
target_include_directories(VantaDspTest PRIVATE Source)

# Stage 3 DSP tests (Granular + Additive), also JUCE-free
add_executable(VantaStage3Test Tests/stage3_test.cpp)
target_include_directories(VantaStage3Test PRIVATE Source)

# Stage 3 completion tests (parameter-ID manifest, voice management, sample safety), JUCE-free
add_executable(VantaStage3bTest Tests/stage3b_test.cpp)
target_include_directories(VantaStage3bTest PRIVATE Source)

# Stage 4 modulation tests (LFO / envelopes / step / matrix / macros / engine integration), JUCE-free
add_executable(VantaStage4Test Tests/stage4_test.cpp)
target_include_directories(VantaStage4Test PRIVATE Source)

enable_testing()
add_test(NAME VantaDspTest      COMMAND VantaDspTest)
add_test(NAME VantaStage3Test   COMMAND VantaStage3Test)
add_test(NAME VantaStage3bTest  COMMAND VantaStage3bTest)
add_test(NAME VantaStage4Test   COMMAND VantaStage4Test)
