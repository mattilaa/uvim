*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui

*** Test Cases ***
Normal Mode On Start
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Quit Uvim

Dot Repeat Change Word
    Create File    ${SAMPLE_FILE}    joo\njoo
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    c
    Send Keys    i
    Send Keys    w
    Expect Mode    INSERT
    Send Keys    jee
    Send Escape
    Expect Mode    NORMAL
    Send Keys    j
    Send Keys    .
    Send Keys    :
    Send Keys    wq
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${SAMPLE_FILE}
    ${lines} =    Split To Lines    ${content}
    ${line1} =    Get From List    ${lines}    0
    ${line2} =    Get From List    ${lines}    1
    Should Be Equal    ${line1}    jee
    Should Be Equal    ${line2}    jee

Normal Mode Replace Char
    Create File    ${SAMPLE_FILE}    joo
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    r
    Send Keys    e
    Expect Mode    NORMAL
    Send Keys    x
    Send Keys    :
    Send Keys    wq
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${SAMPLE_FILE}
    ${lines} =    Split To Lines    ${content}
    ${line1} =    Get From List    ${lines}    0
    Should Be Equal    ${line1}    oo
