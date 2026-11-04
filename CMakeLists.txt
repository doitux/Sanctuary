cmake_minimum_required(VERSION 3.22)
project(Sanctuary VERSION 0.2.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(SANCTUARY_TOOLS "Build command-line renderer / benchmark" ON)

add_library(sanctuary_engine STATIC
  engine/src/Engine.cpp
  engine/src/Presets.cpp)
target_include_directories(sanctuary_engine PUBLIC engine/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sanctuary_engine PRIVATE -O3 -ffast-math -Wall -Wextra)
elseif(MSVC)
  target_compile_options(sanctuary_engine PRIVATE /O2 /fp:fast)
endif()
target_compile_definitions(sanctuary_engine PUBLIC _USE_MATH_DEFINES)
set_target_properties(sanctuary_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(SANCTUARY_TOOLS)
  add_executable(sanctuary-render tools/render/main.cpp)
  target_link_libraries(sanctuary-render PRIVATE sanctuary_engine)
endif()

# ------------------------------------------------------------------ plugin
option(SANCTUARY_PLUGIN "Build the plugin (VST3 / CLAP / LV2 / AU / Standalone)" ON)
if(SANCTUARY_PLUGIN)
  add_subdirectory(libs/JUCE EXCLUDE_FROM_ALL)
  set(SC_FORMATS VST3 LV2 Standalone)
  if(APPLE)
    list(APPEND SC_FORMATS AU)
  endif()
  juce_add_plugin(Sanctuary
    PRODUCT_NAME "Sanctuary"
    COMPANY_NAME "Sanctuary"
    COMPANY_WEBSITE "https://github.com/doitux/Sanctuary"
    BUNDLE_ID "io.github.doitux.sanctuary"
    PLUGIN_MANUFACTURER_CODE Sctu
    PLUGIN_CODE SnPd
    FORMATS ${SC_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    LV2URI "https://github.com/doitux/Sanctuary"
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(Sanctuary PRIVATE plugin/Processor.cpp plugin/Editor.cpp)
  target_compile_definitions(Sanctuary PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
  target_link_libraries(Sanctuary PRIVATE sanctuary_engine
    juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

  add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)
  clap_juce_extensions_plugin(TARGET Sanctuary
    CLAP_ID "io.github.doitux.sanctuary"
    CLAP_FEATURES instrument synthesizer stereo ambient)
endif()

# ------------------------------------------------------------------ test host (Linux): loads the .clap like a DAW would
if(SANCTUARY_PLUGIN AND UNIX AND NOT APPLE)
  add_executable(sanctuary-claphost EXCLUDE_FROM_ALL tools/testhost/claphost.cpp)
  target_include_directories(sanctuary-claphost PRIVATE libs/clap-juce-extensions/clap-libs/clap/include)
  target_link_libraries(sanctuary-claphost PRIVATE ${CMAKE_DL_LIBS})
endif()
