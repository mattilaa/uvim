*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui

*** Test Cases ***
Buffer Browser Mode Entry And Exit
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Ctrl    w
    Sleep    0.2
    Expect Text    Buffers:
    Send Escape
    Expect Mode    NORMAL
    Quit Uvim
