*** Settings ***
Resource    uvim_tui.resource

Suite Setup    Setup Uvim Tui
Suite Teardown    Cleanup Uvim Tui

*** Test Cases ***
Visual Mode Entry And Exit
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    v
    Expect Mode    VISUAL
    Send Escape
    Expect Mode    NORMAL
    Quit Uvim

Keys Sent Right After Escape Are Not Lost
    # Esc and ":qa!" in a single write: the bytes after Esc are not an
    # escape sequence and must still run as a command.
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    v
    Expect Mode    VISUAL
    Send Keys    \x1b:qa!\r
    Wait For Exit
