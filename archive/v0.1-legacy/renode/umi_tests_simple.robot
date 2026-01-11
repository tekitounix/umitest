*** Settings ***
Documentation     Simple UMI-OS Renode Test
Library           String
Library           OperatingSystem

*** Variables ***
${CURDIR}         ${CURDIR}
${UART_LOG}       ${CURDIR}/../.build/renode_uart.log
${ELF}            ${CURDIR}/../.build/renode_test.elf
${PLATFORM}       ${CURDIR}/stm32f4_test.repl
${RESC}           ${CURDIR}/test.resc

*** Test Cases ***
Check Test Binary Exists
    [Documentation]    Verify test binary was built
    File Should Exist    ${ELF}

Check Platform Definition Exists
    [Documentation]    Verify platform definition exists
    File Should Exist    ${PLATFORM}

Check Renode Script Exists
    [Documentation]    Verify Renode script exists
    File Should Exist    ${RESC}

Verify Test Execution Output
    [Documentation]    Run Renode and check output
    [Tags]    integration
    File Should Exist    ${UART_LOG}
    ${output}=    Get File    ${UART_LOG}
    Should Contain    ${output}    UMI-OS
