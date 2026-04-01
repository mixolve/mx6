if(NOT DEFINED PLUGIN_BINARY)
    message(FATAL_ERROR "PLUGIN_BINARY is required")
endif()

if(NOT DEFINED DEPLOY_DIR)
    message(FATAL_ERROR "DEPLOY_DIR is required")
endif()

get_filename_component(plugin_binary_dir "${PLUGIN_BINARY}" DIRECTORY)
get_filename_component(plugin_contents_dir "${plugin_binary_dir}" DIRECTORY)
get_filename_component(plugin_bundle_dir "${plugin_contents_dir}" DIRECTORY)
get_filename_component(plugin_bundle_name "${plugin_bundle_dir}" NAME)

set(destination_bundle "${DEPLOY_DIR}/${plugin_bundle_name}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DEPLOY_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${destination_bundle}"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${plugin_bundle_dir}" "${destination_bundle}"
    COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Deployed ${plugin_bundle_name} to ${DEPLOY_DIR}")
