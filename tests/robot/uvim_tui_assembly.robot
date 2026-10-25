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
    Expect Text Matching    Assembly \\[[^]]+\\] \\[active\\]
    Send Keys    \x1bh
    Expect Text    Source [active]
    Send Keys    v
    Expect Mode    ASM
    Send Keys    v
    Expect Text Matching    Assembly \\[[^]]+\\] \\[active\\]
    Send Escape
    Expect Mode    NORMAL
    Send Keys    :q!
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${file}
    Should Be Equal    ${content}    ${source}

Compilation Database Supplies Flags And Prompt Overrides Them
    ${file} =    Set Variable    ${TEMP_DIR}/configured.cpp
    ${source} =    Set Variable    \#if !defined(__OPTIMIZE__)\n\#error expected prompt optimization\n\#endif\nstatic_assert(VALUE == 7);\nint configured(int x) { return x + VALUE; }
    Create File    ${file}    ${source}
    Create Directory    ${TEMP_DIR}/build
    ${database} =    Set Variable    [{"directory":"${TEMP_DIR}/build","file":"../configured.cpp","arguments":["clang++","-std=c++11","-DVALUE=1","-O0","-Werror","-c","../configured.cpp","-o","original.o","-MMD","-MF","original.d"]}]
    Create File    ${TEMP_DIR}/build/compile_commands.json    ${database}
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${file}
    Expect Mode    NORMAL
    Send Keys    :asm . --split -O2 -std=c++20 -DVALUE=7
    Send Enter
    Expect Text    compile_commands    timeout=15
    Send Keys    G
    Expect Text    source L5
    Send Escape
    Expect Mode    NORMAL
    Send Keys    :q!
    Send Enter
    Wait For Exit
    File Should Not Exist    ${TEMP_DIR}/build/original.o
    File Should Not Exist    ${TEMP_DIR}/build/original.d
    ${content} =    Get File    ${file}
    Should Be Equal    ${content}    ${source}
