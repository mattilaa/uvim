*** Settings ***
Library    OperatingSystem
Library    Process
Library    String

Suite Setup    Setup Uvim Binary
Suite Teardown    Cleanup Temp Dir

*** Variables ***
${TEMP_DIR}    ${EMPTY}
${UVIM_BIN}    ${EMPTY}

*** Keywords ***
Setup Uvim Binary
    ${bin} =    Get Environment Variable    UVIM_BIN    ${CURDIR}/../../build/uvim
    File Should Exist    ${bin}
    Set Suite Variable    ${UVIM_BIN}    ${bin}
    ${tmp_base} =    Get Environment Variable    TMPDIR    /tmp
    ${rand} =    Generate Random String    10
    ${tmp} =    Catenate    SEPARATOR=/    ${tmp_base}    uvim_robot_${rand}
    ${exists} =    Run Keyword And Return Status    Directory Should Exist    ${tmp}
    Run Keyword If    ${exists}    Remove Directory    ${tmp}    recursive=true
    Create Directory    ${tmp}
    Set Suite Variable    ${TEMP_DIR}    ${tmp}
    Create File    ${TEMP_DIR}/.uvim_robot_temp    ok

Cleanup Temp Dir
    ${is_temp} =    Run Keyword And Return Status    File Should Exist    ${TEMP_DIR}/.uvim_robot_temp
    Run Keyword If    ${is_temp}    Remove Directory    ${TEMP_DIR}    recursive=true

Run Uvim
    [Arguments]    @{args}
    ${result} =    Run Process    ${UVIM_BIN}    @{args}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${result.rc}    0
    RETURN    ${result}

Run Uvim Expect Rc
    [Arguments]    ${expected_rc}    @{args}
    ${result} =    Run Process    ${UVIM_BIN}    @{args}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${result.rc}    ${expected_rc}
    RETURN    ${result}

*** Test Cases ***
Version Flag
    ${result} =    Run Uvim    --version
    Should Contain    ${result.stdout}    uvim

Help Flag
    ${result} =    Run Uvim    --help
    Should Contain    ${result.stdout}    Usage:

Init Config Writes File
    ${config_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    uvim.yaml
    Run Uvim    --init-config    ${config_path}
    File Should Exist    ${config_path}
    ${size} =    Get File Size    ${config_path}
    Should Be True    ${size} > 0
    ${config_contents} =    Get File    ${config_path}
    Should Contain    ${config_contents}    editor:

Init Config Fails When File Exists
    ${config_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    uvim_existing.yaml
    Create File    ${config_path}    existing
    ${result} =    Run Uvim Expect Rc    2    --init-config    ${config_path}
    Should Contain    ${result.stderr}    config already exists

Missing Value For Config Flag
    ${result} =    Run Uvim Expect Rc    2    --config
    Should Contain    ${result.stderr}    missing value for option
