*** Settings ***
Library    OperatingSystem
Library    String
Library    ${CURDIR}/uvim_tui.py

*** Variables ***
${TEMP_DIR}    ${EMPTY}
${UVIM_BIN}    ${EMPTY}
${SAMPLE_FILE}    ${EMPTY}

*** Keywords ***
Setup Uvim Tui
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
    ${file_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    sample.txt
    Create File    ${file_path}    hello
    Set Suite Variable    ${SAMPLE_FILE}    ${file_path}

Cleanup Uvim Tui
    Stop Uvim
    ${is_temp} =    Run Keyword And Return Status    Directory Should Exist    ${TEMP_DIR}
    Run Keyword If    ${is_temp}    Remove Directory    ${TEMP_DIR}    recursive=true
    ${pipe_exists} =    Run Keyword And Return Status    File Should Exist    ${CURDIR}/../../PIPE
    Run Keyword If    ${pipe_exists}    Remove File    ${CURDIR}/../../PIPE

Quit Uvim
    Send Keys    :
    Send Keys    q
    Send Enter
    Wait For Exit

Force Quit Uvim
    Send Keys    :
    Send Keys    qa!
    Send Enter
    Wait For Exit

Enter Command Mode
    Wait Until Keyword Succeeds    5x    0.1s    Try Enter Command Mode

Try Enter Command Mode
    Send Escape
    Sleep    0.05
    Send Keys    :
    Expect Mode    COMMAND
