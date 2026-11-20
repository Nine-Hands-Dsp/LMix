cmake_minimum_required(VERSION 3.22)

# macOS : binaire universel (Apple Silicon + Intel), macOS 10.13 minimum (avant project())
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(LMix VERSION 1.1.2)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.15
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(LmixAssets SOURCES
  logo/NineHandsLogoWhite.svg
  fonts/Poppins-Regular.ttf fonts/Poppins-Medium.ttf fonts/Poppins-Bold.ttf fonts/IBMPlexMono-SemiBold.ttf)

juce_add_plugin(Lmix
  COMPANY_NAME "NINE HANDS | DSP"
  PLUGIN_MANUFACTURER_CODE Wiga
  PLUGIN_CODE WgLm
  FORMATS VST3
  VST3_CATEGORIES Fx Dynamics Mastering
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  PRODUCT_NAME "LMix"
  BUNDLE_ID "com.ninehandsdsp.lmix"
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Lmix PRIVATE PluginProcessor.cpp PluginEditor.cpp)

# verrou de taille de la fenetre du DAW (jamais sous 60 %, jamais au-dela de 150 %)
if(APPLE)
  target_sources(Lmix PRIVATE WindowMinSize.mm)
  target_link_libraries(Lmix PRIVATE "-framework Cocoa")
else()
  target_sources(Lmix PRIVATE WindowMinSize.cpp)
endif()
if(WIN32)
  target_link_libraries(Lmix PRIVATE comctl32 user32)
endif()

target_compile_definitions(Lmix PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Lmix
  PRIVATE
    LmixAssets
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
