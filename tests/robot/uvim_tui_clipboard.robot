*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui


*** Test Cases ***
Visual Mode Yank Copies To System Clipboard
    ${test_file} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    clipboard_test.txt
    ${content} =    Catenate    SEPARATOR=\n
    ...    Line 1: First line
    ...    Line 2: Second line
    ...    Line 3: Third line
    Create File    ${test_file}    ${content}

    # Clear system clipboard
    Clear Test Clipboard

    # Start uvim and yank a line
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${test_file}
    Expect Mode    NORMAL

    # Enter visual mode and select first line
    Send Keys    v
    Expect Mode    VISUAL
    Send Keys    $
    Send Keys    y
    Expect Mode    NORMAL

    # Quit uvim
    Send Keys    :
    Send Keys    q
    Send Enter
    Wait For Exit

    # Check if text was copied to system clipboard
    ${clipboard} =    Read Test Clipboard
    Should Contain    ${clipboard}    Line 1: First line

Visual Line Mode Yank Copies To System Clipboard
    ${test_file} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    clipboard_test2.txt
    ${content} =    Catenate    SEPARATOR=\n
    ...    Line A
    ...    Line B
    ...    Line C
    Create File    ${test_file}    ${content}

    # Clear system clipboard
    Clear Test Clipboard

    # Start uvim and yank a line
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${test_file}
    Expect Mode    NORMAL

    # Enter visual line mode and select first line
    Send Keys    V
    Sleep    0.1
    Send Keys    y
    Sleep    0.1

    # Quit uvim
    Send Keys    :
    Send Keys    q
    Send Enter
    Wait For Exit

    # Check if text was copied to system clipboard (should include newline)
    ${clipboard} =    Read Test Clipboard
    Should Contain    ${clipboard}    Line A

Normal Mode YY Copies Line To System Clipboard
    ${test_file} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    clipboard_test3.txt
    ${content} =    Catenate    SEPARATOR=\n
    ...    Test Line 1
    ...    Test Line 2
    ...    Test Line 3
    Create File    ${test_file}    ${content}

    # Clear system clipboard
    Clear Test Clipboard

    # Start uvim and yank a line with yy
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${test_file}
    Expect Mode    NORMAL

    # Yank current line
    Send Keys    yy
    Sleep    0.1

    # Quit uvim
    Send Keys    :
    Send Keys    q
    Send Enter
    Wait For Exit

    # Check if line was copied to system clipboard
    ${clipboard} =    Read Test Clipboard
    Should Contain    ${clipboard}    Test Line 1

Paste From System Clipboard
    ${test_file} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    clipboard_test4.txt
    Create File    ${test_file}    ${EMPTY}

    # Copy text to system clipboard
    Write Test Clipboard    Pasted from clipboard

    # Start uvim and paste
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${test_file}
    Expect Mode    NORMAL

    # Paste from system clipboard
    Send Keys    p
    Sleep    0.1

    # Save and quit
    Send Keys    :
    Send Keys    wq
    Send Enter
    Wait For Exit

    # Check if text was pasted
    ${file_content} =    Get File    ${test_file}
    Should Contain    ${file_content}    Pasted from clipboard
