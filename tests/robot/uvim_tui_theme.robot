*** Settings ***
Resource            uvim_tui.resource

Suite Setup         Setup Uvim Tui
Suite Teardown      Cleanup Uvim Tui


*** Test Cases ***
Theme Statusline Colors Apply
    ${config_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    theme.toml
    ${config} =    Catenate
    ...    SEPARATOR=\n
    ...    [theme.statusline]
    ...    fg = "#010203"
    ...    bg = "#040506"
    Create File    ${config_path}    ${config}
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    --config    ${config_path}    ${SAMPLE_FILE}
    Expect Raw Text    \x1b[38;2;1;2;3m\x1b[48;2;4;5;6m
    Quit Uvim

Theme Cursor Colors Apply
    ${config_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    theme_cursor.toml
    ${config} =    Catenate
    ...    SEPARATOR=\n
    ...    [theme.cursor]
    ...    fg = "#111213"
    ...    bg = "#141516"
    Create File    ${config_path}    ${config}
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    --config    ${config_path}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    v
    Expect Raw Text    \x1b[38;2;17;18;19m\x1b[48;2;20;21;22m
    Send Escape
    Expect Mode    NORMAL
    Force Quit Uvim

Theme Selection Colors Apply
    ${config_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    theme_selection.toml
    ${config} =    Catenate
    ...    SEPARATOR=\n
    ...    [theme.selection]
    ...    fg = "#212223"
    ...    bg = "#242526"
    Create File    ${config_path}    ${config}
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    --config    ${config_path}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    v
    Send Keys    l
    Expect Raw Text    \x1b[38;2;33;34;35m\x1b[48;2;36;37;38m
    Send Escape
    Expect Mode    NORMAL
    Force Quit Uvim

Theme Search Colors Apply
    ${config_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    theme_search.toml
    ${config} =    Catenate
    ...    SEPARATOR=\n
    ...    [theme.search]
    ...    fg = "#313233"
    ...    bg = "#343536"
    Create File    ${config_path}    ${config}
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    --config    ${config_path}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    /
    Send Keys    hello
    Send Enter
    Expect Raw Text    \x1b[38;2;49;50;51m\x1b[48;2;52;53;54m
    Quit Uvim

Theme Panel Colors Apply
    ${config_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    theme_panel.toml
    ${config} =    Catenate
    ...    SEPARATOR=\n
    ...    [theme.panel]
    ...    fg = "#414243"
    ...    bg = "#444546"
    Create File    ${config_path}    ${config}
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    --config    ${config_path}    ${SAMPLE_FILE}
    Expect Mode    NORMAL
    Send Keys    :
    Send Keys    lspinfo
    Send Enter
    Expect Raw Text    \x1b[38;2;65;66;67m\x1b[48;2;68;69;70m
    Send Keys    q
    Quit Uvim

Theme Syntax String Colors Apply
    ${config_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    theme_syntax.toml
    ${config} =    Catenate    SEPARATOR=\n    [theme.syntax]    string = "#515253"
    Create File    ${config_path}    ${config}
    ${cpp_path} =    Catenate    SEPARATOR=/    ${TEMP_DIR}    sample.cpp
    Create File    ${cpp_path}    const char* s = "hi";
    Start Uvim In Dir    ${UVIM_BIN}    ${TEMP_DIR}    --config    ${config_path}    ${cpp_path}
    Expect Raw Text    \x1b[38;2;81;82;83m
    Quit Uvim
