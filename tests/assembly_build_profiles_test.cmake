if(NOT DEFINED UVIM_CONFIG OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "UVIM_CONFIG and TEST_DIRECTORY are required")
endif()

function(check_profile name expected)
    set(directory "${TEST_DIRECTORY}/${name}")
    execute_process(
        COMMAND "${UVIM_CONFIG}" ${ARGN} --build-dir "${directory}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: ${output}\n${error}")
    endif()
    file(READ "${directory}/uvim_config_cache.cmake" cache)
    if(NOT cache MATCHES "set\\(UVIM_ENABLE_ASM_VIEW ${expected} CACHE BOOL")
        message(FATAL_ERROR "${name}: incorrect assembly browser build flag\n${cache}")
    endif()
    file(READ "${directory}/uvim-config.conf" config)
    if(NOT config MATCHES "asm_view=${expected}")
        message(FATAL_ERROR "${name}: assembly browser selection was not persisted")
    endif()
endfunction()

foreach(preset IN ITEMS vi-real vi-min minimal basic)
    check_profile("${preset}" OFF --preset "${preset}")
endforeach()
check_profile(full ON --preset full)
check_profile(disabled OFF --preset full --disable asm-view)
check_profile(custom ON --preset basic --enable asm-view)
check_profile(reset OFF --preset full --preset basic)
check_profile(imported ON --import "${TEST_DIRECTORY}/full/uvim-config.conf")
check_profile(imported-disabled OFF --import "${TEST_DIRECTORY}/full/uvim-config.conf"
              --disable asm-view)
check_profile(imported-enabled ON --import "${TEST_DIRECTORY}/disabled/uvim-config.conf"
              --enable asm-view)
