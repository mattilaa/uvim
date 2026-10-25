*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui

*** Test Cases ***
Buffer Substitute Replaces All Matches AndSaves
    Create File    ${SAMPLE_FILE}    foo foo\nfoo\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :%s/foo/bar/g
    Send Enter
    Expect Mode    NORMAL
    Send Keys    :wq
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${SAMPLE_FILE}
    Should Be Equal    ${content}    bar bar\nbar\n

Project Substitute Stages Files Until WriteAll
    ${second} =    Set Variable    ${TEMP_DIR}/substitute_second.txt
    Create File    ${SAMPLE_FILE}    substitute_old_unique\n
    Create File    ${second}    substitute_old_unique substitute_old_unique\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :%%s/substitute_old_unique/substitute_new_unique/g
    Send Enter
    Expect Mode    NORMAL
    ${before} =    Get File    ${second}
    Should Be Equal    ${before}    substitute_old_unique substitute_old_unique\n
    Send Keys    :wa
    Send Enter
    Send Keys    :qa
    Send Enter
    Wait For Exit
    ${first_content} =    Get File    ${SAMPLE_FILE}
    ${second_content} =    Get File    ${second}
    Should Be Equal    ${first_content}    substitute_new_unique\n
    Should Be Equal    ${second_content}    substitute_new_unique substitute_new_unique\n
