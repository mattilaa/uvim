*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui
Test Teardown    Stop Uvim

*** Test Cases ***
Select Compiler Flag Browse Assembly And Inspect
    ${file} =    Set Variable    ${TEMP_DIR}/assembly.cpp
    ${source} =    Set Variable    int square(int value) { return value * value; }\n
    Create File    ${file}    ${source}
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${file}
    Expect Mode    NORMAL
    Send Keys    :asm . --
    Expect Text    Optimize for speed
    Send Enter
    Expect Text    :asm . -O2
    Send Enter
    Expect Text    clang++ -O2    timeout=15
    Send Keys    j
    Expect Mode    ASM
    Send Keys    i
    Expect Text    Inspect:
    Send Keys    j
    Send Keys    k
    Send Escape
    Expect Mode    NORMAL
    Send Keys    :q!
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${file}
    Should Be Equal    ${content}    ${source}

Escape Cancels Compiler Flags Before Compilation
    ${file} =    Set Variable    ${TEMP_DIR}/cancel.cpp
    Create File    ${file}    int value;\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${file}
    Expect Mode    NORMAL
    Send Keys    :asm . --
    Expect Text    Optimize for speed
    Send Escape
    Expect Mode    NORMAL
    Send Keys    :q!
    Send Enter
    Wait For Exit

Source Assembly Split Supports Linked Navigation And Pane Keys
    ${file} =    Set Variable    ${TEMP_DIR}/linked.cpp
    ${source} =    Set Variable    int square(int value) {\nreturn value * value;\n}\n
    Create File    ${file}    ${source}
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${file}
    Expect Mode    NORMAL
    Send Keys    :set iskitty=false
    Send Enter
    Expect Mode    NORMAL
    Send Keys    :asm . --split -O0
    Send Enter
    Expect Text    clang++ -O0    timeout=15
    Send Keys    j
    Expect Text    source L2
    Send Keys    \x1bl
    Expect Text    Assembly [active]
    Send Keys    \x1bh
    Expect Text    Source [active]
    Send Keys    v
    Expect Mode    ASM
    Send Keys    v
    Expect Text    Assembly [active]
    Send Escape
    Expect Mode    NORMAL
    Send Keys    :q!
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${file}
    Should Be Equal    ${content}    ${source}
