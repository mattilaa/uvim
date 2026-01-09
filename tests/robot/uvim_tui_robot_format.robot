*** Settings ***
Resource            uvim_tui.resource

Suite Setup         Setup Uvim Tui
Suite Teardown      Cleanup Uvim Tui


*** Test Cases ***
Leader F Formats Robot File
    ${robot_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    format.robot
    ${content} =    Catenate    SEPARATOR=\n
    ...    *** Test Cases ***
    ...    Fuzzy Find Mode Entry And Exit
    ...    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    ...    Expect Mode    NORMAL
    ...    Send Ctrl    p
    ...    Sleep    0.2
    ...    Expect Text    Find File:
    ...    Send Escape
    ...    Expect Mode    NORMAL
    ...    Quit Uvim
    Create File    ${robot_path}    ${content}
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${robot_path}
    Expect Mode    NORMAL
    Send Keys    ${SPACE}
    Send Keys    f
    Send Keys    :
    Send Keys    wq
    Send Enter
    Wait For Exit
    ${updated} =    Get File    ${robot_path}
    Should Contain    ${updated}    ${SPACE}${SPACE}${SPACE}${SPACE}Start Uvim In Dir
    Should Not Contain    ${updated}    Start Uvim In Dir
