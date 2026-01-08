*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui

*** Test Cases ***
Command Pwd Shows Working Directory
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    ${basename} =    Fetch From Right    ${TEMP_DIR}    /
    Enter Command Mode
    Send Keys    pwd
    Send Enter
    Expect Text    ${basename}
    Quit Uvim

Command New Buffer Listed
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Enter Command Mode
    Send Keys    enew
    Send Enter
    Wait Until Keyword Succeeds    5x    0.1s    Expect Text    New buffer created
    Force Quit Uvim

Command Write Shows Status
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Wait Until Keyword Succeeds    5x    0.1s    Expect Mode    NORMAL
    Send Keys    i
    Wait Until Keyword Succeeds    5x    0.1s    Expect Mode    INSERT
    Send Keys    robot-write
    Send Escape
    Expect Mode    NORMAL
    Enter Command Mode
    Send Keys    wq
    Send Enter
    Wait For Exit
    Wait Until Keyword Succeeds    10x    0.1s    File Content Should Contain    ${SAMPLE_FILE}    robot-write

*** Keywords ***
File Content Should Contain
    [Arguments]    ${path}    ${text}
    ${contents} =    Get File    ${path}
    Should Contain    ${contents}    ${text}
