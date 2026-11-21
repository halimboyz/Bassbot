cmake_minimum_required(VERSION 3.22)
project(Bassbot VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
# Static CRT on Windows so users don't need the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Bassbot
    PRODUCT_NAME "BASSBOT 2000"
    COMPANY_NAME "Bassbot"
    PLUGIN_MANUFACTURER_CODE Bsbt
    PLUGIN_CODE Bb20
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion)

target_sources(Bassbot PRIVATE Source/Plugin.cpp)
target_compile_definitions(Bassbot PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Bassbot PRIVATE
    juce::juce_audio_processors juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

option(BASSBOT_TESTS "Build the DSP self-check" OFF)
if(BASSBOT_TESTS)
    add_executable(BassbotTest Tests/dsp_test.cpp)
    target_include_directories(BassbotTest PRIVATE Source)
    target_compile_definitions(BassbotTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(BassbotTest PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
endif()
