# Build trees are transferred between CI runners. The interpreter discovered
# during configure may not be installed on the machine that runs CTest.
if(NOT DEFINED UVIM_PYTHON_TEST OR NOT EXISTS "${UVIM_PYTHON_TEST}")
    message(FATAL_ERROR "Python test script does not exist: ${UVIM_PYTHON_TEST}")
endif()

if(NOT DEFINED UVIM_PYTHON_MIN_VERSION)
    set(UVIM_PYTHON_MIN_VERSION 3)
endif()

if(DEFINED UVIM_PYTHON_EXECUTABLE AND EXISTS "${UVIM_PYTHON_EXECUTABLE}")
    set(_uvim_python "${UVIM_PYTHON_EXECUTABLE}")
else()
    find_package(Python3 ${UVIM_PYTHON_MIN_VERSION} COMPONENTS Interpreter REQUIRED)
    set(_uvim_python "${Python3_EXECUTABLE}")
endif()

execute_process(
    COMMAND "${_uvim_python}" "${UVIM_PYTHON_TEST}"
    RESULT_VARIABLE _uvim_python_result
)
if(NOT "${_uvim_python_result}" STREQUAL "0")
    message(FATAL_ERROR
        "Python test ${UVIM_PYTHON_TEST} failed with ${_uvim_python_result} (interpreter: ${_uvim_python})")
endif()
