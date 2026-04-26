*** Settings ***
Library    OperatingSystem
Library    String
Library    Collections
Library    ${CURDIR}/uvim_tui.py

*** Variables ***
${TEMP_DIR}    ${EMPTY}
${UVIM_BIN}    ${EMPTY}
${SAMPLE_FILE}    ${EMPTY}
${CLIPBOARD_BIN_DIR}    ${EMPTY}
${CLIPBOARD_FILE}    ${EMPTY}

*** Keywords ***
Setup Uvim Tui
    ${bin} =    Get Environment Variable    UVIM_BIN    ${CURDIR}/../../build/uvim
    File Should Exist    ${bin}
    Set Suite Variable    ${UVIM_BIN}    ${bin}
    ${tmp_base} =    Get Environment Variable    TMPDIR    /tmp
    ${rand} =    Generate Random String    10
    ${tmp} =    Catenate    SEPARATOR=/    ${tmp_base}    uvim_robot_${rand}
    ${exists} =    Run Keyword And Return Status    Directory Should Exist    ${tmp}
    Run Keyword If    ${exists}    Remove Directory    ${tmp}    recursive=true
    Create Directory    ${tmp}
    Set Suite Variable    ${TEMP_DIR}    ${tmp}
    Setup Fake Clipboard
    ${file_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    sample.txt
    Create File    ${file_path}    hello
    Set Suite Variable    ${SAMPLE_FILE}    ${file_path}

Cleanup Uvim Tui
    Stop Uvim
    ${is_temp} =    Run Keyword And Return Status    Directory Should Exist    ${TEMP_DIR}
    Run Keyword If    ${is_temp}    Remove Directory    ${TEMP_DIR}    recursive=true
    ${pipe_exists} =    Run Keyword And Return Status    File Should Exist    ${CURDIR}/../../PIPE
    Run Keyword If    ${pipe_exists}    Remove File    ${CURDIR}/../../PIPE

Setup Fake Clipboard
    ${orig_path} =    Get Environment Variable    PATH
    ${clip_dir} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    fake_clipboard_bin
    ${clip_file} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    fake_clipboard.txt
    Create Directory    ${clip_dir}
    Create File    ${clip_file}    ${EMPTY}
    Set Suite Variable    ${CLIPBOARD_BIN_DIR}    ${clip_dir}
    Set Suite Variable    ${CLIPBOARD_FILE}    ${clip_file}
    ${pbcopy_script} =    Catenate    SEPARATOR=\n
    ...    #!/bin/sh
    ...    cat > "${UVIM_TEST_CLIPBOARD_FILE}"
    ${pbpaste_script} =    Catenate    SEPARATOR=\n
    ...    #!/bin/sh
    ...    cat "${UVIM_TEST_CLIPBOARD_FILE}"
    ${xclip_script} =    Catenate    SEPARATOR=\n
    ...    #!/bin/sh
    ...    want_output=0
    ...    for arg in "$@"; do
    ...        if [ "$arg" = "-o" ]; then
    ...            want_output=1
    ...        fi
    ...    done
    ...    if [ "$want_output" -eq 1 ]; then
    ...        cat "${UVIM_TEST_CLIPBOARD_FILE}"
    ...    else
    ...        cat > "${UVIM_TEST_CLIPBOARD_FILE}"
    ...    fi
    ${xsel_script} =    Catenate    SEPARATOR=\n
    ...    #!/bin/sh
    ...    want_output=0
    ...    for arg in "$@"; do
    ...        if [ "$arg" = "--output" ]; then
    ...            want_output=1
    ...        fi
    ...    done
    ...    if [ "$want_output" -eq 1 ]; then
    ...        cat "${UVIM_TEST_CLIPBOARD_FILE}"
    ...    else
    ...        cat > "${UVIM_TEST_CLIPBOARD_FILE}"
    ...    fi
    Create File    ${clip_dir}/pbcopy    ${pbcopy_script}
    Create File    ${clip_dir}/pbpaste    ${pbpaste_script}
    Create File    ${clip_dir}/xclip    ${xclip_script}
    Create File    ${clip_dir}/xsel    ${xsel_script}
    Run    chmod +x "${clip_dir}/pbcopy" "${clip_dir}/pbpaste" "${clip_dir}/xclip" "${clip_dir}/xsel"
    Set Environment Variable    UVIM_TEST_CLIPBOARD_FILE    ${clip_file}
    Set Environment Variable    PATH    ${clip_dir}:${orig_path}

Clear Test Clipboard
    Create File    ${CLIPBOARD_FILE}    ${EMPTY}

Read Test Clipboard
    ${clipboard} =    Get File    ${CLIPBOARD_FILE}
    [Return]    ${clipboard}

Write Test Clipboard
    [Arguments]    ${text}
    Create File    ${CLIPBOARD_FILE}    ${text}

Quit Uvim
    Send Keys    :
    Send Keys    q
    Send Enter
    Wait For Exit

Force Quit Uvim
    Send Keys    :
    Send Keys    qa!
    Send Enter
    Wait For Exit

Enter Command Mode
    Wait Until Keyword Succeeds    5x    0.1s    Try Enter Command Mode

Try Enter Command Mode
    Send Escape
    Sleep    0.05
    Send Keys    :
    Expect Mode    COMMAND

Return To Normal Mode
    Wait Until Keyword Succeeds    10x    0.1s    Try Return To Normal Mode

Try Return To Normal Mode
    Send Escape
    Sleep    0.05
    Expect Mode    NORMAL    timeout=0.5
