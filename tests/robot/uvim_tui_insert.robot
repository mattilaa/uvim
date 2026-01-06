*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui

*** Test Cases ***
Insert Mode Entry And Exit
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    i
    Expect Mode    INSERT
    Send Escape
    Expect Mode    NORMAL
    Quit Uvim
