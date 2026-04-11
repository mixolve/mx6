cmake_minimum_required(VERSION 3.22)

set(mxe_target_system_name "${CMAKE_SYSTEM_NAME}")

if(mxe_target_system_name STREQUAL "")
    set(mxe_target_system_name "${CMAKE_HOST_SYSTEM_NAME}")
endif()

set(mxe_project_languages C CXX)

if(mxe_target_system_name STREQUAL "Darwin")
    list(APPEND mxe_project_languages OBJC OBJCXX)
endif()

project(mxe VERSION 0.1.0 LANGUAGES ${mxe_project_languages})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MXE_SDK_SHIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apple-sdk-shims")
option(MXE_USE_LOCAL_JUCE "Use a local JUCE checkout via JUCE_DIR" OFF)

if(APPLE)
    set(MXE_DEFAULT_DEPLOY_DIR "/Library/Audio/Plug-Ins/ALL")
    set(MXE_DEFAULT_RELEASE_ARCHIVE_NAME "mxe-macos.zip")
    set(MXE_ENABLE_VST3_DEPLOY_DEFAULT ON)
elseif(WIN32)
    if(DEFINED ENV{CommonProgramW6432} AND NOT "$ENV{CommonProgramW6432}" STREQUAL "")
        set(MXE_DEFAULT_DEPLOY_DIR "$ENV{CommonProgramW6432}/VST3")
    elseif(DEFINED ENV{COMMONPROGRAMFILES} AND NOT "$ENV{COMMONPROGRAMFILES}" STREQUAL "")
        set(MXE_DEFAULT_DEPLOY_DIR "$ENV{COMMONPROGRAMFILES}/VST3")
    else()
        set(MXE_DEFAULT_DEPLOY_DIR "C:/Program Files/Common Files/VST3")
    endif()

    set(MXE_DEFAULT_RELEASE_ARCHIVE_NAME "mxe-windows.zip")
    set(MXE_ENABLE_VST3_DEPLOY_DEFAULT OFF)
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" mxe_platform_suffix)

    if(mxe_platform_suffix STREQUAL "")
        set(mxe_platform_suffix "unknown")
    endif()

    set(MXE_DEFAULT_DEPLOY_DIR "")
    set(MXE_DEFAULT_RELEASE_ARCHIVE_NAME "mxe-${mxe_platform_suffix}.zip")
    set(MXE_ENABLE_VST3_DEPLOY_DEFAULT OFF)
endif()

option(MXE_ENABLE_VST3_DEPLOY "Deploy the built VST3 bundle after build" ${MXE_ENABLE_VST3_DEPLOY_DEFAULT})
set(MXE_DEPLOY_DIR "${MXE_DEFAULT_DEPLOY_DIR}" CACHE PATH "Directory where the built VST3 bundle will be deployed after build")
set(MXE_ARCHIVE_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Directory where the packaged VST3 zip will be written after build")
set(MXE_RELEASE_ARCHIVE_NAME "${MXE_DEFAULT_RELEASE_ARCHIVE_NAME}" CACHE STRING "Filename for the packaged VST3 release archive")
set(MXE_PLUGIN_BUNDLE_NAME "mxe.vst3")
set(MXE_PACKAGE_STAGE_DIR "${MXE_ARCHIVE_OUTPUT_DIR}/${MXE_PLUGIN_BUNDLE_NAME}")
set(MXE_PACKAGE_ZIP_PATH "${MXE_ARCHIVE_OUTPUT_DIR}/${MXE_RELEASE_ARCHIVE_NAME}")

if(APPLE)
    if(DEFINED ENV{CPATH} AND NOT "$ENV{CPATH}" STREQUAL "")
        set(ENV{CPATH} "${MXE_SDK_SHIM_DIR}:$ENV{CPATH}")
    else()
        set(ENV{CPATH} "${MXE_SDK_SHIM_DIR}")
    endif()
endif()

if(MXE_USE_LOCAL_JUCE)
    if(NOT DEFINED JUCE_DIR)
        message(FATAL_ERROR "Set -DJUCE_DIR=/absolute/path/to/JUCE when MXE_USE_LOCAL_JUCE=ON")
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

juce_add_binary_data(mxe_assets
    SOURCES
        assets/fonts/SometypeMono-Bold.ttf
        assets/fonts/SometypeMono-Regular.ttf
        assets/fonts/SometypeMono-OFL.txt
)

juce_add_plugin(mxe
    COMPANY_NAME "mixolve"
    BUNDLE_ID "com.mixolve.mxe"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE MxLv
    PLUGIN_CODE Mxe0
    FORMATS VST3
    PRODUCT_NAME "mxe"
)

juce_generate_juce_header(mxe)

target_sources(mxe
    PRIVATE
        source/PluginEditor.cpp
        source/PluginEditor.h
        source/PluginProcessor.cpp
        source/PluginProcessor.h
        source/dsp/DspCore.cpp
        source/dsp/DspCore.h
        source/dsp/MultibandCrossover.cpp
        source/dsp/MultibandCrossover.h
        source/dsp/MultibandProcessor.cpp
        source/dsp/MultibandProcessor.h
)

target_compile_definitions(mxe
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_include_directories(mxe
    PRIVATE
        "${MXE_SDK_SHIM_DIR}"
)

target_link_libraries(mxe
    PRIVATE
        mxe_assets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(MXE_ENABLE_VST3_DEPLOY AND NOT MXE_DEPLOY_DIR STREQUAL "")
    add_custom_command(TARGET mxe_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MXE_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${MXE_DEPLOY_DIR}/${MXE_PLUGIN_BUNDLE_NAME}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:mxe_VST3>" "${MXE_DEPLOY_DIR}/${MXE_PLUGIN_BUNDLE_NAME}"
        VERBATIM
    )
endif()

add_custom_command(TARGET mxe_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MXE_ARCHIVE_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${MXE_PACKAGE_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:mxe_VST3>" "${MXE_PACKAGE_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${MXE_PACKAGE_ZIP_PATH}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${MXE_ARCHIVE_OUTPUT_DIR}" ${CMAKE_COMMAND} -E tar "cf" "${MXE_PACKAGE_ZIP_PATH}" --format=zip -- "${MXE_PLUGIN_BUNDLE_NAME}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${MXE_PACKAGE_STAGE_DIR}"
    VERBATIM
)
