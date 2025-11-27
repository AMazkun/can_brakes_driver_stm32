*** Settings ***
Suite Setup                   Setup
Suite Teardown                Teardown
Test Setup                    Reset Emulation
Test Teardown                 Test Teardown
Resource                      ${RENODEKEYWORDS}

*** Variables ***
${UART}                       sysbus.usart2
${FDCAN}                      sysbus.fdcan1
${ADC}                        sysbus.adc1
${TIM1}                       sysbus.tim1
${PLATFORM}                   @stm32g431.repl
${BINARY}                     @firmware.elf

# CAN Message IDs
${HEARTBEAT_ID}               0x98FF0D00
${BRAKE_CMD_ID}               0x98FF0D09
${BRAKE_MSG_ID}               0x98FF0D0A

# Node IDs
${MCU_NODE_ID}                0xF0
${PC_NODE_ID}                 0x10

# ADC positions
${POS_RELEASED}               200
${POS_PUSHED}                 3800
${POS_MIDWAY}                 2000

*** Keywords ***
Create Machine
    Execute Command          mach create "brake_test"
    Execute Command          machine LoadPlatformDescription ${PLATFORM}
    Execute Command          sysbus LoadELF ${BINARY}

Setup
    Setup
    Create Machine

Send PC Heartbeat
    [Arguments]              ${msg_count}=0
    # Pack Heart_Beat_MSG from PC (Node_id=0x10)
    ${data}=                 Pack CAN Message  node_id=0x10  msg_count=${msg_count}  health=1  stamp=1000
    Execute Command          ${FDCAN} SendMessage ${HEARTBEAT_ID} ${data} true

Send Push Command
    [Arguments]              ${msg_id}=1
    # Pack Left_Brake_CMD: push (brake_state=1)
    ${data}=                 Pack Brake Command  msg_id=${msg_id}  brake_state=1
    Execute Command          ${FDCAN} SendMessage ${BRAKE_CMD_ID} ${data} true

Send Release Command
    [Arguments]              ${msg_id}=2
    # Pack Left_Brake_CMD: release (brake_state=0)
    ${data}=                 Pack Brake Command  msg_id=${msg_id}  brake_state=0
    Execute Command          ${FDCAN} SendMessage ${BRAKE_CMD_ID} ${data} true

Set Brake Position
    [Arguments]              ${position}
    Execute Command          ${ADC} FeedSample ${position} 2

Wait For CAN Message
    [Arguments]              ${can_id}  ${timeout}=5s
    ${msg}=                  Wait For CAN Frame  ${FDCAN}  ${can_id}  timeout=${timeout}
    [Return]                 ${msg}

Verify MCU Heartbeat
    ${msg}=                  Wait For CAN Message  ${HEARTBEAT_ID}
    ${node_id}=              Get Byte From Message  ${msg}  0
    Should Be Equal          ${node_id}  ${MCU_NODE_ID}
    Log                      MCU Heartbeat received: Node_id=${node_id}

Verify Brake Telemetry
    [Arguments]              ${expected_state}
    ${msg}=                  Wait For CAN Message  ${BRAKE_MSG_ID}
    ${flags}=                Get Byte From Message  ${msg}  3
    
    # Check state flags
    ${releasing}=            Evaluate  ${flags} & 0x01
    ${released}=             Evaluate  (${flags} >> 1) & 0x01
    ${pushing}=              Evaluate  (${flags} >> 2) & 0x01
    ${pushed}=               Evaluate  (${flags} >> 3) & 0x01
    
    Run Keyword If           '${expected_state}' == 'RELEASED'     Should Be True  ${released} == 1
    Run Keyword If           '${expected_state}' == 'RELEASING'    Should Be True  ${releasing} == 1
    Run Keyword If           '${expected_state}' == 'PUSHED'       Should Be True  ${pushed} == 1
    Run Keyword If           '${expected_state}' == 'PUSHING'      Should Be True  ${pushing} == 1
    
    Log                      Brake state verified: ${expected_state}

*** Test Cases ***
Test 001: MCU Sends Heartbeat After Boot
    [Documentation]          Verify MCU starts sending heartbeat messages
    [Tags]                   heartbeat  basic
    
    Start Emulation
    
    # Wait for initialization (1 second)
    Sleep                    1s
    
    # MCU should send heartbeat every 50ms
    Verify MCU Heartbeat
    
    Log                      ✓ MCU heartbeat transmission confirmed

Test 002: MCU Responds To PC Heartbeat
    [Documentation]          Verify MCU monitors PC heartbeat (Node_id=0x10)
    [Tags]                   heartbeat  communication
    
    Start Emulation
    Sleep                    1s
    
    # Send PC heartbeat
    Send PC Heartbeat        msg_count=100
    
    # Wait a bit
    Sleep                    100ms
    
    # MCU should continue normal operation
    Verify MCU Heartbeat
    
    Log                      ✓ MCU accepts PC heartbeat

Test 003: Initial Brake State Is Released
    [Documentation]          Verify brake starts in RELEASED state
    [Tags]                   brake  initial
    
    # Set ADC to released position
    Set Brake Position       ${POS_RELEASED}
    
    Start Emulation
    Sleep                    1s
    
    # Check first telemetry message
    Verify Brake Telemetry   RELEASED
    
    Log                      ✓ Initial brake state is RELEASED

Test 004: Push Command Execution
    [Documentation]          Test brake push command from PC
    [Tags]                   brake  command  push
    
    Set Brake Position       ${POS_RELEASED}
    Start Emulation
    Sleep                    1s
    
    # Send push command
    Send Push Command        msg_id=1
    Sleep                    100ms
    
    # Verify brake enters PUSHING state
    Verify Brake Telemetry   PUSHING
    
    # Simulate movement to pushed position
    Set Brake Position       ${POS_PUSHED}
    Sleep                    500ms
    
    # Verify brake reaches PUSHED state
    Verify Brake Telemetry   PUSHED
    
    Log                      ✓ Push command executed successfully

Test 005: Release Command Execution
    [Documentation]          Test brake release command from PC
    [Tags]                   brake  command  release
    
    Set Brake Position       ${POS_PUSHED}
    Start Emulation
    Sleep                    1s
    
    # Send release command
    Send Release Command     msg_id=2
    Sleep                    100ms
    
    # Verify brake enters RELEASING state
    Verify Brake Telemetry   RELEASING
    
    # Simulate movement to released position
    Set Brake Position       ${POS_RELEASED}
    Sleep                    500ms
    
    # Verify brake reaches RELEASED state
    Verify Brake Telemetry   RELEASED
    
    Log                      ✓ Release command executed successfully

Test 006: Full Cycle Push And Release
    [Documentation]          Test complete push-release cycle
    [Tags]                   brake  cycle  integration
    
    Set Brake Position       ${POS_RELEASED}
    Start Emulation
    Sleep                    1s
    
    # Initial state
    Verify Brake Telemetry   RELEASED
    
    # Push
    Send Push Command        msg_id=1
    Sleep                    100ms
    Verify Brake Telemetry   PUSHING
    
    Set Brake Position       ${POS_PUSHED}
    Sleep                    500ms
    Verify Brake Telemetry   PUSHED
    
    # Release
    Send Release Command     msg_id=2
    Sleep                    100ms
    Verify Brake Telemetry   RELEASING
    
    Set Brake Position       ${POS_RELEASED}
    Sleep                    500ms
    Verify Brake Telemetry   RELEASED
    
    Log                      ✓ Full push-release cycle completed

Test 007: PC Heartbeat Timeout Detection
    [Documentation]          Verify MCU detects PC heartbeat timeout
    [Tags]                   watchdog  timeout  safety
    
    Start Emulation
    Sleep                    1s
    
    # Send PC heartbeat initially
    Send PC Heartbeat        msg_count=1
    Sleep                    100ms
    
    # Verify MCU is happy
    ${msg1}=                 Wait For CAN Message  ${HEARTBEAT_ID}
    ${health1}=              Get Byte From Message  ${msg1}  5
    Should Be Equal          ${health1}  1  # Health = ON
    
    # Stop PC heartbeat (simulate timeout)
    # Wait > 200ms (watchdog timeout)
    Sleep                    300ms
    
    # MCU health should change to WARNING
    ${msg2}=                 Wait For CAN Message  ${HEARTBEAT_ID}
    ${health2}=              Get Byte From Message  ${msg2}  5
    Should Be Equal          ${health2}  3  # Health = WARNING
    
    Log                      ✓ PC timeout detected, MCU health = WARNING

Test 008: Heartbeat Message Counter Increments
    [Documentation]          Verify MSG_Count increments in MCU heartbeat
    [Tags]                   heartbeat  counter
    
    Start Emulation
    Sleep                    1s
    
    # Get first heartbeat
    ${msg1}=                 Wait For CAN Message  ${HEARTBEAT_ID}
    ${count1}=               Get DWord From Message  ${msg1}  1
    
    # Wait for next heartbeat
    Sleep                    100ms
    ${msg2}=                 Wait For CAN Message  ${HEARTBEAT_ID}
    ${count2}=               Get DWord From Message  ${msg2}  1
    
    # Counter should increment
    ${expected}=             Evaluate  ${count1} + 1
    Should Be True           ${count2} >= ${expected}
    
    Log                      ✓ Heartbeat counter increments: ${count1} -> ${count2}

Test 009: Telemetry Period Is 100ms
    [Documentation]          Verify telemetry is sent every 100ms
    [Tags]                   telemetry  timing
    
    Start Emulation
    Sleep                    1s
    
    # Measure time between telemetry messages
    ${time1}=                Get Time
    ${msg1}=                 Wait For CAN Message  ${BRAKE_MSG_ID}
    
    ${time2}=                Get Time
    ${msg2}=                 Wait For CAN Message  ${BRAKE_MSG_ID}
    
    ${interval}=             Evaluate  ${time2} - ${time1}
    
    # Should be approximately 100ms (allow ±20ms tolerance)
    Should Be True           80 <= ${interval} <= 120
    
    Log                      ✓ Telemetry period: ${interval}ms

Test 010: Duplicate Commands Are Idempotent
    [Documentation]          Verify duplicate commands don't cause issues
    [Tags]                   brake  command  robustness
    
    Set Brake Position       ${POS_RELEASED}
    Start Emulation
    Sleep                    1s
    
    # Send push command twice
    Send Push Command        msg_id=1
    Sleep                    50ms
    Send Push Command        msg_id=2
    Sleep                    100ms
    
    # Should still be in PUSHING state (not error)
    Verify Brake Telemetry   PUSHING
    
    # Complete movement
    Set Brake Position       ${POS_PUSHED}
    Sleep                    500ms
    
    Verify Brake Telemetry   PUSHED
    
    Log                      ✓ Duplicate commands handled correctly

Test 011: Invalid Brake State Is Rejected
    [Documentation]          Verify invalid brake_state values are ignored
    [Tags]                   brake  validation  safety
    
    Set Brake Position       ${POS_RELEASED}
    Start Emulation
    Sleep                    1s
    
    Verify Brake Telemetry   RELEASED
    
    # Send invalid brake_state (value=5, should be 0 or 1)
    ${invalid_data}=         Pack Brake Command  msg_id=99  brake_state=5
    Execute Command          ${FDCAN} SendMessage ${BRAKE_CMD_ID} ${invalid_data} true
    
    Sleep                    200ms
    
    # Brake should remain in RELEASED state
    Verify Brake Telemetry   RELEASED
    
    Log                      ✓ Invalid brake_state rejected

Test 012: Time To End Operation Updates
    [Documentation]          Verify time_to_end_operation field updates
    [Tags]                   telemetry  timing
    
    Set Brake Position       ${POS_RELEASED}
    Start Emulation
    Sleep                    1s
    
    # Send push command
    Send Push Command        msg_id=1
    Sleep                    100ms
    
    # Get first telemetry during PUSHING
    ${msg1}=                 Wait For CAN Message  ${BRAKE_MSG_ID}
    ${time1}=                Get Word From Message  ${msg1}  6
    
    # Wait a bit
    Sleep                    500ms
    
    # Get second telemetry
    ${msg2}=                 Wait For CAN Message  ${BRAKE_MSG_ID}
    ${time2}=                Get Word From Message  ${msg2}  6
    
    # Time should decrease or become 0 when complete
    Should Be True           ${time2} <= ${time1}
    
    Log                      ✓ time_to_end_operation: ${time1}ms -> ${time2}ms

Test 013: ADC Position Affects State Transitions
    [Documentation]          Verify position feedback controls state machine
    [Tags]                   brake  adc  position
    
    Set Brake Position       ${POS_RELEASED}
    Start Emulation
    Sleep                    1s
    
    # Send push command
    Send Push Command        msg_id=1
    Sleep                    100ms
    
    Verify Brake Telemetry   PUSHING
    
    # Keep position at released - should stay in PUSHING
    Sleep                    500ms
    Verify Brake Telemetry   PUSHING
    
    # Now move to pushed position
    Set Brake Position       ${POS_PUSHED}
    Sleep                    200ms
    
    # Should transition to PUSHED
    Verify Brake Telemetry   PUSHED
    
    Log                      ✓ ADC position controls state transitions

Test 014: System Initialization Sequence
    [Documentation]          Verify proper initialization and startup
    [Tags]                   init  startup
    
    Set Brake Position       ${POS_RELEASED}
    Start Emulation
    
    # Within first second, health should be INIT
    Sleep                    500ms
    ${msg1}=                 Wait For CAN Message  ${HEARTBEAT_ID}
    ${health1}=              Get Byte From Message  ${msg1}  5
    Should Be Equal          ${health1}  2  # Health = INIT
    
    # After 1 second, should transition to ON
    Sleep                    1s
    ${msg2}=                 Wait For CAN Message  ${HEARTBEAT_ID}
    ${health2}=              Get Byte From Message  ${msg2}  5
    Should Be Equal          ${health2}  1  # Health = ON
    
    Log                      ✓ Initialization sequence: INIT -> ON

Test 015: Bidirectional Heartbeat Communication
    [Documentation]          Verify both PC and MCU heartbeats work together
    [Tags]                   heartbeat  bidirectional
    
    Start Emulation
    Sleep                    1s
    
    # Send PC heartbeat
    Send PC Heartbeat        msg_count=1
    
    # Verify MCU heartbeat with correct Node_id
    ${msg}=                  Wait For CAN Message  ${HEARTBEAT_ID}
    ${node_id}=              Get Byte From Message  ${msg}  0
    Should Be Equal          ${node_id}  ${MCU_NODE_ID}
    
    # Send more PC heartbeats
    :FOR  ${i}  IN RANGE  5
    \    Send PC Heartbeat  msg_count=${i}
    \    Sleep              50ms
    
    # MCU should remain healthy
    ${msg_final}=            Wait For CAN Message  ${HEARTBEAT_ID}
    ${health}=               Get Byte From Message  ${msg_final}  5
    Should Be Equal          ${health}  1  # Health = ON
    
    Log                      ✓ Bidirectional heartbeat working

*** Comments ***
Additional test scenarios to implement:
- Test 016: Motor PWM duty cycle control
- Test 017: Emergency stop functionality
- Test 018: CAN bus error handling
- Test 019: Multiple rapid commands
- Test 020: Long-term stability test