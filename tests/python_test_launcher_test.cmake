find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "TEST_DIRECTORY is required")
endif()
file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
set(_probe "${TEST_DIRECTORY}/python probe.py")
file(WRITE "${_probe}" "print('uvim python launcher ran')\n")
set(_launcher "${CMAKE_CURRENT_LIST_DIR}/run_python_test.cmake")

# Simulate a downloaded CTest build tree whose original Python is missing.
foreach(_interpreter IN ITEMS "${Python3_EXECUTABLE}" "${TEST_DIRECTORY}/missing Python/python.exe")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DUVIM_PYTHON_EXECUTABLE=${_interpreter}"
            "-DUVIM_PYTHON_TEST=${_probe}"
            -P "${_launcher}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
    if(NOT "${_result}" STREQUAL "0" OR NOT _output MATCHES "uvim python launcher ran")
        message(FATAL_ERROR "Python launch failed for ${_interpreter}: ${_output}\n${_error}")
    endif()
endforeach()

# Running through CMake must still fail CTest when Python reports a failure.
file(WRITE "${_probe}" "raise SystemExit(7)\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DUVIM_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
        "-DUVIM_PYTHON_TEST=${_probe}"
        -P "${_launcher}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
)
if("${_result}" STREQUAL "0")
    message(FATAL_ERROR "The launcher swallowed a Python test failure")
endif()
