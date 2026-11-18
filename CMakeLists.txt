cmake_minimum_required(VERSION 3.22)

project(AIStemSplitter VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(AIStemSplitter
    COMPANY_NAME "StemTools"
    PRODUCT_NAME "AI Stem Splitter by Oliver Tkach"
    PLUGIN_MANUFACTURER_CODE Stmt
    PLUGIN_CODE Asps
    FORMATS VST3
    VST3_CATEGORIES Instrument Tools
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# el motor Python va embebido en el plugin y se escribe en disco al usarlo
juce_add_binary_data(AIStemSplitterData SOURCES engine/stem_engine.py)

target_sources(AIStemSplitter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EngineRunner.cpp)

target_compile_definitions(AIStemSplitter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AIStemSplitter
    PRIVATE
        AIStemSplitterData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
