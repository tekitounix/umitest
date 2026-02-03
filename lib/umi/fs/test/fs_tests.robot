*** Settings ***
Documentation     UMI-OS Filesystem Tests on Renode (Cortex-M4)
Library           String
Library           OperatingSystem

*** Variables ***
${UART_LOG}       ${CURDIR}/../../../build/renode_fs_uart.log
${ELF}            ${CURDIR}/../../../build/renode_fs_test/release/renode_fs_test.elf

*** Test Cases ***
Check FS Test Binary Exists
    [Documentation]    Verify filesystem test binary was built
    File Should Exist    ${ELF}

Verify Littlefs Tests Pass
    [Documentation]    Check littlefs test output
    [Tags]    integration
    File Should Exist    ${UART_LOG}
    ${output}=    Get File    ${UART_LOG}
    Should Contain    ${output}    --- littlefs Tests ---
    Should Not Contain    ${output}    [FAIL] lfs:

Verify FATfs Tests Pass
    [Documentation]    Check FATfs test output
    [Tags]    integration
    File Should Exist    ${UART_LOG}
    ${output}=    Get File    ${UART_LOG}
    Should Contain    ${output}    --- FATfs Tests ---
    Should Not Contain    ${output}    [FAIL] fat:

Verify All Tests Pass
    [Documentation]    Check overall test result
    [Tags]    integration
    File Should Exist    ${UART_LOG}
    ${output}=    Get File    ${UART_LOG}
    Should Contain    ${output}    *** ALL TESTS PASSED ***
    Should Contain    ${output}    TEST_COMPLETE
