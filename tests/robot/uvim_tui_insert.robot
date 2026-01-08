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

Insert Braces Inside String
    Create File    ${SAMPLE_FILE}    std::string f = "";
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    f"
    Send Keys    l
    Send Keys    i
    Expect Mode    INSERT
    Send Keys    {}
    Send Escape
    Expect Mode    NORMAL
    Send Keys    :
    Send Keys    wq
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${SAMPLE_FILE}
    ${lines} =    Split To Lines    ${content}
    ${line1} =    Get From List    ${lines}    0
    Should Be Equal    ${line1}    std::string f = "{}";
