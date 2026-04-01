cmake_minimum_required(VERSION 3.22)

set(mx6_target_system_name "${CMAKE_SYSTEM_NAME}")

if(mx6_target_system_name STREQUAL "")
    set(mx6_target_system_name "${CMAKE_HOST_SYSTEM_NAME}")
endif()

set(mx6_project_languages C CXX)

if(mx6_target_system_name STREQUAL "Darwin")
    list(APPEND mx6_project_languages OBJC OBJCXX)
endif()

project(mx6 VERSION 0.1.0 LANGUAGES ${mx6_project_languages})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MX6_SDK_SHIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk-shims")
option(MX6_USE_LOCAL_JUCE "Use a local JUCE checkout via JUCE_DIR" OFF)

if(APPLE)
    set(MX6_DEFAULT_DEPLOY_DIR "/Library/Audio/Plug-Ins/ALL")
    set(MX6_DEFAULT_RELEASE_ARCHIVE_NAME "mx6-macos.zip")
    set(MX6_ENABLE_VST3_DEPLOY_DEFAULT ON)
elseif(WIN32)
    if(DEFINED ENV{CommonProgramW6432} AND NOT "$ENV{CommonProgramW6432}" STREQUAL "")
        set(MX6_DEFAULT_DEPLOY_DIR "$ENV{CommonProgramW6432}/VST3")
    elseif(DEFINED ENV{COMMONPROGRAMFILES} AND NOT "$ENV{COMMONPROGRAMFILES}" STREQUAL "")
        set(MX6_DEFAULT_DEPLOY_DIR "$ENV{COMMONPROGRAMFILES}/VST3")
    else()
        set(MX6_DEFAULT_DEPLOY_DIR "C:/Program Files/Common Files/VST3")
    endif()

    set(MX6_DEFAULT_RELEASE_ARCHIVE_NAME "mx6-windows.zip")
    set(MX6_ENABLE_VST3_DEPLOY_DEFAULT OFF)
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" mx6_platform_suffix)

    if(mx6_platform_suffix STREQUAL "")
        set(mx6_platform_suffix "unknown")
    endif()

    set(MX6_DEFAULT_DEPLOY_DIR "")
    set(MX6_DEFAULT_RELEASE_ARCHIVE_NAME "mx6-${mx6_platform_suffix}.zip")
    set(MX6_ENABLE_VST3_DEPLOY_DEFAULT OFF)
endif()

option(MX6_ENABLE_VST3_DEPLOY "Deploy the built VST3 bundle after build" ${MX6_ENABLE_VST3_DEPLOY_DEFAULT})
set(MX6_DEPLOY_DIR "${MX6_DEFAULT_DEPLOY_DIR}" CACHE PATH "Directory where the built VST3 bundle will be deployed after build")
set(MX6_ARCHIVE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Directory where the packaged VST3 zip will be written after build")
set(MX6_RELEASE_ARCHIVE_NAME "${MX6_DEFAULT_RELEASE_ARCHIVE_NAME}" CACHE STRING "Filename for the packaged VST3 release archive")

if(APPLE)
    if(DEFINED ENV{CPATH} AND NOT "$ENV{CPATH}" STREQUAL "")
        set(ENV{CPATH} "${MX6_SDK_SHIM_DIR}:$ENV{CPATH}")
    else()
        set(ENV{CPATH} "${MX6_SDK_SHIM_DIR}")
    endif()
endif()

if(MX6_USE_LOCAL_JUCE)
    if(NOT DEFINED JUCE_DIR)
        message(FATAL_ERROR "Set -DJUCE_DIR=/absolute/path/to/JUCE when MX6_USE_LOCAL_JUCE=ON")
    endif()

    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)

    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
    )

    FetchContent_MakeAvailable(juce)
endif()

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

if(MX6_ENABLE_VST3_DEPLOY AND NOT MX6_DEPLOY_DIR STREQUAL "")
    add_custom_command(TARGET mx6_VST3 POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DPLUGIN_BINARY=$<TARGET_FILE:mx6_VST3>
            -DDEPLOY_DIR=${MX6_DEPLOY_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployVST3.cmake"
        VERBATIM
    )
endif()

add_custom_command(TARGET mx6_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        -DPLUGIN_BINARY=$<TARGET_FILE:mx6_VST3>
        -DOUTPUT_DIR=${MX6_ARCHIVE_OUTPUT_DIR}
        -DARCHIVE_NAME=${MX6_RELEASE_ARCHIVE_NAME}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageVST3Archive.cmake"
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
