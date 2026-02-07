*** Settings ***
Library           Renode
Suite Setup       Setup
Suite Teardown    Teardown
Test Timeout      30 seconds

*** Keywords ***
Setup
    Execute Command    mach create "stm32f4"
    Execute Command    machine LoadPlatformDescription @platforms/cpus/stm32f4.repl
    ${elf_path}=       Evaluate    "${CURDIR}/../../../build/umibench_stm32f4_renode/release/umibench_stm32f4_renode.elf"
    Execute Command    sysbus LoadELF @${elf_path}
    Execute Command    showAnalyzer sysbus.uart0
    Execute Command    start

Teardown
    Execute Command    quit

*** Test Cases ***
Bench Should Output Test Message
    Wait For Line On Uart    umibench STM32F4 Renode Test    timeout=10
    Wait For Line On Uart    Test completed!    timeout=10
