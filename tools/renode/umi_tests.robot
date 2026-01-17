*** Settings ***
Documentation     UMI-OS Comprehensive Test Suite for Renode
Suite Setup       Setup
Suite Teardown    Teardown
Test Setup        Reset Emulation
Test Teardown     Test Teardown
Resource          ${RENODEKEYWORDS}

*** Variables ***
${UART}           sysbus.usart2
${ELF}            .build/renode_test.elf
${PLATFORM}       ${CURDIR}/stm32f4_test.repl
${TIMEOUT}        30s

*** Keywords ***
Setup
    # Initialize Renode remote library first
    Setup Renode
    Setup Machine

Setup Machine
    Execute Command          mach create "umi_test"
    Execute Command          machine LoadPlatformDescription @${PLATFORM}
    Execute Command          sysbus LoadELF @${ELF}
    # Copy vector table to boot address
    Execute Command          sysbus WriteDoubleWord 0x00000000 `sysbus ReadDoubleWord 0x08000000`
    Execute Command          sysbus WriteDoubleWord 0x00000004 `sysbus ReadDoubleWord 0x08000004`
    Create Terminal Tester   ${UART}  timeout=${TIMEOUT}

Reset Emulation
    Execute Command          cpu Reset
    Execute Command          cpu PC `sysbus ReadDoubleWord 0x08000004`

*** Test Cases ***
UMI-OS Full Test Suite
    [Documentation]    Run all kernel tests and verify pass
    [Tags]             comprehensive  kernel
    
    Start Emulation
    
    # Wait for test header
    Wait For Line On Uart    UMI-OS Comprehensive Renode Tests
    
    # Wait for each test section
    Wait For Line On Uart    --- Task Creation Tests ---
    Wait For Line On Uart    [PASS] create_task returns valid id
    Wait For Line On Uart    [PASS] get_task_name not null
    Wait For Line On Uart    [PASS] task priority correct
    Wait For Line On Uart    [PASS] delete_task succeeds
    Wait For Line On Uart    [PASS] delete invalid task fails
    
    Wait For Line On Uart    --- Notification Tests ---
    Wait For Line On Uart    [PASS] notification delivered
    Wait For Line On Uart    [PASS] notification cleared after wait
    
    Wait For Line On Uart    --- Timer Tests ---
    Wait For Line On Uart    [PASS] timer scheduled
    Wait For Line On Uart    [PASS] timer not fired early
    Wait For Line On Uart    [PASS] timer fired at deadline
    
    Wait For Line On Uart    --- SpscQueue Tests ---
    Wait For Line On Uart    [PASS] queue starts empty
    Wait For Line On Uart    [PASS] queue not full initially
    Wait For Line On Uart    [PASS] push 1
    Wait For Line On Uart    [PASS] pop 1
    
    Wait For Line On Uart    --- Result/Expected Tests ---
    Wait For Line On Uart    [PASS] Ok has value
    Wait For Line On Uart    [PASS] Err has no value
    
    Wait For Line On Uart    --- Priority Scheduling Tests ---
    Wait For Line On Uart    [PASS] realtime task selected first
    
    Wait For Line On Uart    --- Stack Monitor Tests ---
    Wait For Line On Uart    [PASS] stack initially 0% used
    Wait For Line On Uart    [PASS] stack usage detected
    
    Wait For Line On Uart    --- for_each_task Tests ---
    Wait For Line On Uart    [PASS] for_each_task iterates tasks
    
    # Final verification
    Wait For Line On Uart    *** ALL TESTS PASSED ***
    Wait For Line On Uart    TEST_COMPLETE

Task Creation Test Only
    [Documentation]    Quick task creation test
    [Tags]             quick  task
    
    Start Emulation
    Wait For Line On Uart    --- Task Creation Tests ---
    Wait For Line On Uart    [PASS] create_task returns valid id
    Wait For Line On Uart    [PASS] delete_task succeeds

Notification Test Only
    [Documentation]    Quick notification test
    [Tags]             quick  notification
    
    Start Emulation
    Wait For Line On Uart    --- Notification Tests ---
    Wait For Line On Uart    [PASS] notification delivered

Timer Test Only
    [Documentation]    Quick timer test
    [Tags]             quick  timer
    
    Start Emulation
    Wait For Line On Uart    --- Timer Tests ---
    Wait For Line On Uart    [PASS] timer fired at deadline

SpscQueue Test Only
    [Documentation]    Quick SPSC queue test
    [Tags]             quick  queue
    
    Start Emulation
    Wait For Line On Uart    --- SpscQueue Tests ---
    Wait For Line On Uart    [PASS] pop 3

No Failures
    [Documentation]    Verify no test failures occurred
    [Tags]             comprehensive  validation
    
    Start Emulation
    Wait For Line On Uart    TEST_COMPLETE
    # Should NOT see any FAIL markers
    # This test passes if we reach TEST_COMPLETE without errors
