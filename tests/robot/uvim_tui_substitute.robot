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

Confirm Buffer Substitute Can Skip And Approve
    Create File    ${SAMPLE_FILE}    foo foo\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :%s/foo/bar/gc
    Send Enter
    Expect Text Matching    (?s)Confirm replacement.*Match: foo.*Replace with: bar
    Send Keys    ny
    Expect Text    1 substitutions on 1 lines
    Send Keys    :wq
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${SAMPLE_FILE}
    Should Be Equal    ${content}    foo bar\n

Confirm Project Substitute Can Accept Remaining Files
    ${second} =    Set Variable    ${TEMP_DIR}/substitute_confirm_second.txt
    Create File    ${SAMPLE_FILE}    confirm_old_unique confirm_old_unique\n
    Create File    ${second}    confirm_old_unique\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :%%s/confirm_old_unique/confirm_new_unique/gc
    Send Enter
    Expect Text    Confirm replacement
    Send Keys    yna
    Expect Text    2 modified buffers
    Send Keys    :wa
    Send Enter
    Send Keys    :qa
    Send Enter
    Wait For Exit
    ${first_content} =    Get File    ${SAMPLE_FILE}
    ${second_content} =    Get File    ${second}
    Should Be Equal    ${first_content}    confirm_new_unique confirm_old_unique\n
    Should Be Equal    ${second_content}    confirm_new_unique\n
Confirm Quit Leaves Remaining Matches Unchanged
    Create File    ${SAMPLE_FILE}    foo foo\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :%s/foo/bar/gc
    Send Enter
    Expect Text    Confirm replacement
    Send Keys    yq
    Expect Text    1 substitutions on 1 lines; stopped
    Send Keys    :wq
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${SAMPLE_FILE}
    Should Be Equal    ${content}    bar foo\n

Batch Picker Applies Only Green Selected Rows
    Create File    ${SAMPLE_FILE}    foo foo foo\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :%s/foo/bar/gp
    Send Enter
    Expect Text Matching    (?s)Select replacements.*0 selected / 3 matches
    Send Keys    ${SPACE}
    ${green_bg} =    Evaluate    chr(27) + '[48;2;56;120;72m'
    Expect Raw Text    ${green_bg}
    Send Keys    j
    Send Keys    j
    Send Keys    ${SPACE}
    Expect Text    2 selected / 3 matches
    Send Enter
    Expect Text    2 substitutions on 1 lines
    Send Keys    :wq
    Send Enter
    Wait For Exit
    ${content} =    Get File    ${SAMPLE_FILE}
    Should Be Equal    ${content}    bar foo bar\n

Batch Picker Reset ClearsAllSelections
    Create File    ${SAMPLE_FILE}    foo foo\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :%s/foo/bar/gp
    Send Enter
    Expect Text    Select replacements
    Send Keys    ${SPACE}
    Send Keys    j
    Send Keys    ${SPACE}
    Expect Text    2 selected / 2 matches
    Send Keys    r
    Send Enter
    Expect Text    0 substitutions on 0 lines
    Quit Uvim
    ${content} =    Get File    ${SAMPLE_FILE}
    Should Be Equal    ${content}    foo foo\n

Project Batch Picker Stages Only Selected Occurrences
    ${second} =    Set Variable    ${TEMP_DIR}/substitute_batch_second.txt
    Create File    ${SAMPLE_FILE}    batch_old_unique\n
    Create File    ${second}    batch_old_unique batch_old_unique\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :%%s/batch_old_unique/batch_new_unique/gp
    Send Enter
    Expect Text Matching    (?s)Select replacements.*0 selected / 3 matches
    Send Keys    j
    Send Keys    j
    Send Keys    ${SPACE}
    Send Enter
    Expect Text    1 substitutions in 1 project files
    ${before} =    Get File    ${second}
    Should Be Equal    ${before}    batch_old_unique batch_old_unique\n
    Send Keys    :wa
    Send Enter
    Send Keys    :qa
    Send Enter
    Wait For Exit
    ${first_content} =    Get File    ${SAMPLE_FILE}
    ${second_content} =    Get File    ${second}
    Should Be Equal    ${first_content}    batch_old_unique\n
    Should Be Equal    ${second_content}    batch_old_unique batch_new_unique\n

Batch Picker Cancel DiscardsSelections
    Create File    ${SAMPLE_FILE}    foo foo\n
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :%s/foo/bar/gp
    Send Enter
    Expect Text    Select replacements
    Send Keys    ${SPACE}
    Send Escape
    Expect Text    Replacement selection cancelled
    Quit Uvim
    ${content} =    Get File    ${SAMPLE_FILE}
    Should Be Equal    ${content}    foo foo\n
