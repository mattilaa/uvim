*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui

*** Test Cases ***
File Browser Mode Entry And Exit
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    ${SPACE}
    Send Keys    e
    Sleep    0.2
    Expect Text    BROWSE
    Send Keys    q
    Expect Mode    NORMAL
    Quit Uvim
