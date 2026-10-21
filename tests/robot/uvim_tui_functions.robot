*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui
Test Teardown    Stop Uvim

*** Test Cases ***
Browse Function Signatures And Jump To Selected Line
    ${file} =    Set Variable    ${TEMP_DIR}/functions.py
    Create File    ${file}    def first(value: int):\n${SPACE}${SPACE}${SPACE}${SPACE}return value\n\ndef second(name: str):\n${SPACE}${SPACE}${SPACE}${SPACE}return name\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${file}
    Expect Mode    NORMAL
    Send Keys    ${SPACE}o
    Expect Mode    FUNCTIONS
    Send Ctrl    j
    Send Ctrl    k
    Send Ctrl    j
    Send Enter
    Expect Mode    NORMAL
    Send Keys    ciw
    Expect Mode    INSERT
    Send Keys    selected
    Send Escape
    Expect Mode    NORMAL
    Send Keys    :wq
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${file}
    Should Contain    ${content}    def first(value: int):
    Should Contain    ${content}    def selected(name: str):

Filter Function Signatures And Cancel Empty Results
    ${file} =    Set Variable    ${TEMP_DIR}/filtered.py
    Create File    ${file}    def first():\n${SPACE}${SPACE}${SPACE}${SPACE}return 1\n\ndef second():\n${SPACE}${SPACE}${SPACE}${SPACE}return 2\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${file}
    Expect Mode    NORMAL
    Send Keys    :functions
    Send Enter
    Expect Mode    FUNCTIONS
    Send Keys    zzzzz
    Expect Text    No matching function signatures
    Send Enter
    Send Ctrl    u
    Send Keys    second
    Send Enter
    Expect Mode    NORMAL
    Send Keys    ${SPACE}o
    Expect Mode    FUNCTIONS
    Send Escape
    Expect Mode    NORMAL
    Send Keys    ciw
    Expect Mode    INSERT
    Send Keys    filtered
    Send Escape
    Expect Mode    NORMAL
    Send Keys    :wq
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${file}
    Should Contain    ${content}    def first():
    Should Contain    ${content}    def filtered():
