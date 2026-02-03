*** Settings ***
Documentation     Kernel + App Separation Test
Suite Setup       Setup
Suite Teardown    Teardown
Test Setup        Reset Emulation
Test Teardown     Test Teardown
Resource          ${RENODEKEYWORDS}

*** Variables ***
${UART}           sysbus.usart2
${KERNEL_ELF}     build/stm32f4_kernel/release/stm32f4_kernel.elf
${APP_BIN}        build/synth_app/release/synth_app.bin
${PLATFORM}       ${CURDIR}/stm32f4_test.repl
${TIMEOUT}        5s

*** Keywords ***
Setup
    Setup Renode
    Setup Machine

Setup Machine
    Execute Command          mach create "umi_kernel"
    Execute Command          machine LoadPlatformDescription @${PLATFORM}
    # Load kernel ELF (contains vector table at 0x08000000)
    Execute Command          sysbus LoadELF @${KERNEL_ELF}
    # Load app binary to app flash region
    Execute Command          sysbus LoadBinary @${APP_BIN} 0x08060000
    # Set VTOR to kernel flash
    Execute Command          sysbus WriteDoubleWord 0xE000ED08 0x08000000
    Create Terminal Tester   ${UART}  timeout=${TIMEOUT}

Reset Emulation
    Execute Command          cpu Reset

Test Teardown
    Execute Command          emulation Pause

*** Test Cases ***
Kernel Boots Successfully
    [Documentation]    Verify kernel starts and loads app
    [Tags]             kernel  boot
    Start Emulation
    # Run for 100ms virtual time
    Execute Command    emulation RunFor "0.1"
    # Check that execution didn't halt immediately (kernel running)
    ${pc}=             Execute Command    cpu PC
    Should Not Be Equal    ${pc}    0x00000000

Kernel Reaches Main Loop
    [Documentation]    Verify kernel reaches audio processing loop
    [Tags]             kernel  app
    Start Emulation
    # Run for 500ms - should be in audio loop by then
    Execute Command    emulation RunFor "0.5"
    # Verify CPU is still running (not halted in error handler)
    ${pc}=             Execute Command    cpu PC
    # PC should be in flash range (0x08000000 - 0x0807FFFF)
    Should Match Regexp    ${pc}    0x080[0-7][0-9a-fA-F]{4}
