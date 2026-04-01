if(NOT DEFINED PLUGIN_BINARY)
    message(FATAL_ERROR "PLUGIN_BINARY is required")
endif()

if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "OUTPUT_DIR is required")
endif()

if(NOT DEFINED ARCHIVE_NAME)
    message(FATAL_ERROR "ARCHIVE_NAME is required")
endif()

get_filename_component(plugin_binary_dir "${PLUGIN_BINARY}" DIRECTORY)
get_filename_component(plugin_contents_dir "${plugin_binary_dir}" DIRECTORY)
get_filename_component(plugin_bundle_dir "${plugin_contents_dir}" DIRECTORY)
get_filename_component(plugin_bundle_name "${plugin_bundle_dir}" NAME)

set(copied_bundle "${OUTPUT_DIR}/${plugin_bundle_name}")
set(archive_path "${OUTPUT_DIR}/${ARCHIVE_NAME}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${copied_bundle}"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E remove -f "${archive_path}"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${plugin_bundle_dir}" "${copied_bundle}"
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar "cf" "${archive_path}" --format=zip "${plugin_bundle_name}"
    WORKING_DIRECTORY "${OUTPUT_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${copied_bundle}"
    COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Packaged ${plugin_bundle_name} to ${archive_path}")
