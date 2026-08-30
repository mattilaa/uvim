*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui

*** Test Cases ***
Command Mode Entry And Exit
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :
    Expect Mode    COMMAND
    Send Escape
    Expect Mode    NORMAL
    Quit Uvim

Quit With No Changes
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :
    Send Keys    q
    Send Enter
    Wait For Exit

Quit With Changes Requires Force
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    i
    Send Keys    x
    Send Escape
    Send Keys    :
    Send Keys    q
    Send Enter
    Send Keys    :
    Send Keys    q!
    Send Enter
    Wait For Exit

Command Mode From Welcome
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}
    Expect Text    Getting started:
    Send Keys    :
    Expect Mode    COMMAND
    Send Keys    q
    Send Enter
    Wait For Exit
