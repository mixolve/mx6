cmake_minimum_required(VERSION 3.22)

project(mx6 VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MX6_SDK_SHIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk-shims")
set(MX6_DEPLOY_DIR "/Library/Audio/Plug-Ins/ALL" CACHE PATH "Directory where the built VST3 bundle will be deployed after build")
set(MX6_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Directory where the built VST3 bundle will be copied after build")

if(APPLE)
    if(DEFINED ENV{CPATH} AND NOT "$ENV{CPATH}" STREQUAL "")
        set(ENV{CPATH} "${MX6_SDK_SHIM_DIR}:$ENV{CPATH}")
    else()
        set(ENV{CPATH} "${MX6_SDK_SHIM_DIR}")
    endif()
endif()

add_subdirectory(external/JUCE)

juce_add_binary_data(mx6_assets
    SOURCES
        assets/fonts/SometypeMono-Bold.ttf
        assets/fonts/SometypeMono-Regular.ttf
        assets/fonts/SometypeMono-OFL.txt
)

juce_add_plugin(mx6
    COMPANY_NAME "mixolve"
    BUNDLE_ID "com.mixolve.mx6"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE MxLv
    PLUGIN_CODE Mx60
    FORMATS VST3
    PRODUCT_NAME "mx6"
)

juce_generate_juce_header(mx6)

target_sources(mx6
    PRIVATE
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/dsp/DspCore.cpp
        src/dsp/DspCore.h
        src/dsp/MultibandCrossover.cpp
        src/dsp/MultibandCrossover.h
        src/dsp/MultibandProcessor.cpp
        src/dsp/MultibandProcessor.h
)

target_compile_definitions(mx6
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_include_directories(mx6
    PRIVATE
        "${MX6_SDK_SHIM_DIR}"
)

target_link_libraries(mx6
    PRIVATE
        mx6_assets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

add_custom_command(TARGET mx6_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        -DPLUGIN_BINARY=$<TARGET_FILE:mx6_VST3>
        -DDEPLOY_DIR=${MX6_DEPLOY_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployVST3.cmake"
    VERBATIM
)

add_custom_command(TARGET mx6_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        -DPLUGIN_BINARY=$<TARGET_FILE:mx6_VST3>
        -DDEPLOY_DIR=${MX6_DIST_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployVST3.cmake"
    VERBATIM
)

juce_add_console_app(mx6_raw_render
    PRODUCT_NAME "mx6_raw_render"
)

juce_generate_juce_header(mx6_raw_render)

target_sources(mx6_raw_render
    PRIVATE
        tools/mx6_raw_render.cpp
        src/dsp/DspCore.cpp
        src/dsp/DspCore.h
)

target_compile_definitions(mx6_raw_render
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_include_directories(mx6_raw_render
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${MX6_SDK_SHIM_DIR}"
)

target_link_libraries(mx6_raw_render
    PRIVATE
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_add_console_app(mx6_multiband_sanity
    PRODUCT_NAME "mx6_multiband_sanity"
)

juce_generate_juce_header(mx6_multiband_sanity)

target_sources(mx6_multiband_sanity
    PRIVATE
        tools/mx6_multiband_sanity.cpp
        src/dsp/DspCore.cpp
        src/dsp/DspCore.h
        src/dsp/MultibandCrossover.cpp
        src/dsp/MultibandCrossover.h
        src/dsp/MultibandProcessor.cpp
        src/dsp/MultibandProcessor.h
)

target_compile_definitions(mx6_multiband_sanity
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_include_directories(mx6_multiband_sanity
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${MX6_SDK_SHIM_DIR}"
)

target_link_libraries(mx6_multiband_sanity
    PRIVATE
        juce::juce_audio_basics
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
