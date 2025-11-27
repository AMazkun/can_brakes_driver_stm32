/**
 * @file controller.c
 * @brief Main business logic controller for brake actuator system
 * 
 * Handles:
 * - CAN message processing (heartbeat, commands, telemetry)
 * - Periodic message transmission
 * - Status LED indication
 * - System health monitoring
 */

#include "common.h"
#include "can.h"
#include "left_break.h"
#include "automate.h"
#include "main.h"

/* ============================================================================
 * Private Constants
 * ============================================================================ */

/* Node IDs as per specification */
#define NODE_ID_MCU                     0xF0    /* MCU identifier */
#define NODE_ID_PC                      0x10    /* PC identifier */

#define HEARTBEAT_INTERVAL_MS           AUTOMATE_HEART_BEAT_MSG_CYCLE_TIME_MS    /* 50 ms */
#define TELEMETRY_INTERVAL_MS           AUTOMATE_LEFT_BRAKE_CMD_CYCLE_TIME_MS   /* 100 ms */
#define STATUS_LED_BLINK_PERIOD_MS      500     /* 500 ms for blinking */
#define WATCHDOG_TIMEOUT_MS             200     /* PC heartbeat timeout (4 missed heartbeats @ 50ms) */

/* ============================================================================
 * Private Variables
 * ============================================================================ */

/* Controller state - MCU node */
static uint8_t node_id = NODE_ID_MCU;                   /* MCU identifier (0xF0) */
static uint32_t heartbeat_msg_count = 0;                /* MCU heartbeat counter */
static uint8_t node_health = AUTOMATE_HEART_BEAT_MSG_HEALTH_INIT_CHOICE;

/* Timing tracking */
static uint32_t last_heartbeat_tick = 0;                /* Last MCU heartbeat sent */
static uint32_t last_telemetry_tick = 0;                /* Last telemetry sent */
static uint32_t last_pc_heartbeat_tick = 0;             /* Last PC heartbeat received */
static uint32_t last_led_toggle_tick = 0;

/* Message ID counter for telemetry */
static uint8_t telemetry_msg_id = 0;                    /* Left_Brake_MSG counter */

/* LED blink state */
static bool led_state = false;

/* PC heartbeat monitoring */
static uint32_t pc_heartbeat_msg_count = 0;             /* Last received PC MSG_Count */
static bool pc_heartbeat_received = false;               /* Flag: at least one PC heartbeat received */

/* ============================================================================
 * Private Function Prototypes
 * ============================================================================ */

static void SendHeartbeat(void);
static void SendTelemetry(void);
static void ProcessReceivedMessage(void);
static void UpdateSystemHealth(void);
static void UpdateStatusLED(void);

/* ============================================================================
 * Private Functions
 * ============================================================================ */

/**
 * @brief Send heartbeat message
 * 
 * Transmits periodic MCU heartbeat with:
 * - Node ID: 0xF0 (MCU identifier)
 * - Message counter (incrementing for each heartbeat)
 * - Health status (current MCU state)
 * - Timestamp (MCU system time in milliseconds)
 * 
 * This signals to PC that MCU is active and operational.
 */
static void SendHeartbeat(void)
{
    struct automate_heart_beat_msg_t hb_msg;
    uint8_t tx_data[8];
    
    /* Initialize message structure to zeros */
    automate_heart_beat_msg_init(&hb_msg);
    
    /* Fill MCU heartbeat data */
    hb_msg.node_id = NODE_ID_MCU;                        /* 0xF0 - MCU identifier */
    hb_msg.msg_count = heartbeat_msg_count++;            /* Increment MCU heartbeat counter */
    hb_msg.health = node_health;                         /* Current MCU health status */
    hb_msg.stamp = (uint16_t)(HAL_GetTick() & 0xFFFF);  /* MCU timestamp */
    
    /* Pack message into byte array */
    int packed_len = automate_heart_beat_msg_pack(tx_data, &hb_msg, sizeof(tx_data));
    
    /* Send if packing successful */
    if (packed_len > 0) {
        CAN_Driver_Send(AUTOMATE_HEART_BEAT_MSG_FRAME_ID, tx_data, (uint8_t)packed_len);
    }
}

/**
 * @brief Send brake telemetry message
 * 
 * Transmits current brake actuator state to PC:
 * - Message ID counter (incrementing for each telemetry message)
 * - MCU timestamp
 * - Current operation state flags:
 *   * Brake_Releasing: MCU is releasing brake
 *   * Brake_Released: Brake is fully released
 *   * Brake_Pushing: MCU is pushing brake
 *   * Brake_Pushed: Brake is fully pushed
 * - Time_to_end_operation: Estimated time until operation completes
 * 
 * PC uses this data to monitor command execution.
 */
static void SendTelemetry(void)
{
    struct automate_left_brake_msg_t brake_msg;
    uint8_t tx_data[8];
    
    /* Initialize message structure to zeros */
    automate_left_brake_msg_init(&brake_msg);
    
    /* Fill telemetry data */
    brake_msg.msg_id = telemetry_msg_id++;               /* Increment telemetry counter */
    brake_msg.stamp = (uint16_t)(HAL_GetTick() & 0xFFFF); /* MCU timestamp */
    
    /* Set state flags based on current brake state */
    brake_msg.brake_releasing = (app_state.state == BRAKE_STATE_RELEASING) ? 1 : 0;
    brake_msg.brake_released = (app_state.state == BRAKE_STATE_RELEASED) ? 1 : 0;
    brake_msg.brake_pushing = (app_state.state == BRAKE_STATE_PUSHING) ? 1 : 0;
    brake_msg.brake_pushed = (app_state.state == BRAKE_STATE_PUSHED) ? 1 : 0;
    
    /* Get estimated time to end of operation from brake module */
    brake_msg.time_to_end_operation = Brake_GetTimeToEnd();
    
    /* Pack message into byte array */
    int packed_len = automate_left_brake_msg_pack(tx_data, &brake_msg, sizeof(tx_data));
    
    /* Send if packing successful */
    if (packed_len > 0) {
        CAN_Driver_Send(AUTOMATE_LEFT_BRAKE_MSG_FRAME_ID, tx_data, (uint8_t)packed_len);
    }
}

/**
 * @brief Process received CAN messages
 * 
 * Polls CAN driver for new messages and processes them based on ID:
 * - Heart_Beat_MSG (0x98FF0D00): Monitor PC heartbeat (Node_id = 0x10)
 * - Left_Brake_CMD (0x98FF0D09): Execute brake commands from PC
 * 
 * Both PC and MCU send Heart_Beat_MSG. Distinction is made by Node_id field.
 */
static void ProcessReceivedMessage(void)
{
    CAN_Message_t msg;

    /* Process all available messages in queue */
    while (CAN_Driver_Get(&msg)) {
        
        /* Dispatch based on message ID */
        switch (msg.id)
        {
            case AUTOMATE_HEART_BEAT_MSG_FRAME_ID:  /* 0x98FF0D00 */
            {
                struct automate_heart_beat_msg_t heartbeat;
                
                /* Unpack and validate message */
                if (automate_heart_beat_msg_unpack(&heartbeat, msg.data, msg.len) == 0) {
                    
                    /* Check if this is PC heartbeat (Node_id = 0x10) */
                    if (heartbeat.node_id == NODE_ID_PC) {
                        /* Update PC heartbeat timestamp for watchdog */
                        last_pc_heartbeat_tick = HAL_GetTick();
                        pc_heartbeat_msg_count = heartbeat.msg_count;
                        pc_heartbeat_received = true;
                        
                        /* Monitor PC health status */
                        if (heartbeat.health >= AUTOMATE_HEART_BEAT_MSG_HEALTH_WARNING_CHOICE) {
                            /* PC reports warning/failure - could take protective action */
                            /* Example: stop brake operation if PC has critical failure */
                        }
                    }
                    /* Ignore heartbeats from other nodes (including our own MCU echoes) */
                }
                break;
            }
            
            case AUTOMATE_LEFT_BRAKE_CMD_FRAME_ID:  /* 0x98FF0D09 */
            {
                struct automate_left_brake_cmd_t brake_cmd;
                
                /* Unpack and validate message */
                if (automate_left_brake_cmd_unpack(&brake_cmd, msg.data, msg.len) == 0) {
                    /* Validate brake state value (0 or 1) */
                    if (automate_left_brake_cmd_brake_state_is_in_range(brake_cmd.brake_state)) {
                        /* Forward command to brake control module */
                        /* brake_cmd.brake_state: 0 = release, 1 = push */
                        /* brake_cmd.msg_id: command counter from PC */
                        /* brake_cmd.stamp: timestamp when PC formed command */
                        Brake_ProcessCommand(brake_cmd.brake_state);
                    }
                }
                break;
            }
            
            default:
                /* Unknown message ID - ignore */
                break;
        }
    }
}

/**
 * @brief Update system health status
 * 
 * Monitors system state and updates MCU health accordingly:
 * - INIT: Initial startup state (first second)
 * - ON: Normal operation with PC communication
 * - WARNING: PC heartbeat timeout (no messages for 200ms = 4 missed @ 50ms)
 * - FAILURE: Critical error detected
 * 
 * PC heartbeat monitoring:
 * - PC sends Heart_Beat_MSG every 50ms with Node_id = 0x10
 * - MCU monitors MSG_Count and Stamp for communication health
 * - Timeout triggers WARNING state
 */
static void UpdateSystemHealth(void)
{
    uint32_t current_tick = HAL_GetTick();
    
    /* Check for PC heartbeat timeout (only if we've received at least one) */
    if (pc_heartbeat_received) {
        uint32_t time_since_last_pc_heartbeat = current_tick - last_pc_heartbeat_tick;
        
        if (time_since_last_pc_heartbeat > WATCHDOG_TIMEOUT_MS) {
            /* PC heartbeat lost - communication timeout */
            if (node_health == AUTOMATE_HEART_BEAT_MSG_HEALTH_ON_CHOICE) {
                node_health = AUTOMATE_HEART_BEAT_MSG_HEALTH_WARNING_CHOICE;
            }
        } else {
            /* PC heartbeat OK - restore normal health if in warning */
            if (node_health == AUTOMATE_HEART_BEAT_MSG_HEALTH_WARNING_CHOICE) {
                node_health = AUTOMATE_HEART_BEAT_MSG_HEALTH_ON_CHOICE;
            }
        }
    }
    
    /* After initialization period, switch to ON state */
    if (node_health == AUTOMATE_HEART_BEAT_MSG_HEALTH_INIT_CHOICE && 
        current_tick > 1000) {  /* 1 second after boot */
        node_health = AUTOMATE_HEART_BEAT_MSG_HEALTH_ON_CHOICE;
    }
    
    /* Check for brake system errors */
    if (Brake_HasError()) {
        /* Brake system in error state */
        if (node_health < AUTOMATE_HEART_BEAT_MSG_HEALTH_FAILURE_CHOICE) {
            node_health = AUTOMATE_HEART_BEAT_MSG_HEALTH_FAILURE_CHOICE;
        }
    }
}

/**
 * @brief Update status LED based on system state
 * 
 * LED behavior:
 * - RELEASED: OFF
 * - RELEASING: Slow blink (500ms)
 * - PUSHING: Slow blink (500ms)
 * - PUSHED: ON
 * - STOPPED: Fast blink (error indication)
 */
static void UpdateStatusLED(void)
{
    uint32_t current_tick = HAL_GetTick();
    uint32_t blink_period = STATUS_LED_BLINK_PERIOD_MS;
    
    switch(app_state.state) {
        case BRAKE_STATE_RELEASED:
            /* Solid OFF */
            HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
            led_state = false;
            break;
            
        case BRAKE_STATE_PUSHED:
            /* Solid ON */
            HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_SET);
            led_state = true;
            break;
            
        case BRAKE_STATE_RELEASING:
        case BRAKE_STATE_PUSHING:
            /* Slow blink during operation */
            if (current_tick - last_led_toggle_tick >= blink_period) {
                led_state = !led_state;
                HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, 
                                 led_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
                last_led_toggle_tick = current_tick;
            }
            break;
            
        case BRAKE_STATE_STOPPED:
            /* Fast blink for error/stopped state */
            blink_period = STATUS_LED_BLINK_PERIOD_MS / 4;  /* 125 ms */
            if (current_tick - last_led_toggle_tick >= blink_period) {
                led_state = !led_state;
                HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, 
                                 led_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
                last_led_toggle_tick = current_tick;
            }
            break;
            
        default:
            /* Unknown state - turn LED off */
            HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
            break;
    }
}

/* ============================================================================
 * Public Functions
 * ============================================================================ */

/**
 * @brief Initialize controller
 * 
 * Call this once at startup to initialize controller state.
 * Sets MCU Node_id to 0xF0 as per specification.
 */
void Controller_Init(void)
{
    /* Initialize state variables */
    node_id = NODE_ID_MCU;                  /* 0xF0 - MCU identifier */
    heartbeat_msg_count = 0;                /* MCU heartbeat counter starts at 0 */
    telemetry_msg_id = 0;                   /* Telemetry counter starts at 0 */
    node_health = AUTOMATE_HEART_BEAT_MSG_HEALTH_INIT_CHOICE;
    
    /* Initialize timing */
    last_heartbeat_tick = HAL_GetTick();
    last_telemetry_tick = HAL_GetTick();
    last_pc_heartbeat_tick = 0;             /* No PC heartbeat received yet */
    last_led_toggle_tick = HAL_GetTick();
    
    /* Initialize PC monitoring */
    pc_heartbeat_msg_count = 0;
    pc_heartbeat_received = false;
    
    led_state = false;
}

/**
 * @brief Main business logic loop
 * 
 * Call this function repeatedly from main loop.
 * Implements bidirectional communication protocol:
 * 
 * MCU → PC:
 * - Heart_Beat_MSG (0x98FF0D00): Every 50ms with Node_id=0xF0
 * - Left_Brake_MSG (0x98FF0D0A): Every 100ms with actuator state
 * 
 * PC → MCU:
 * - Heart_Beat_MSG (0x98FF0D00): Expected every 50ms with Node_id=0x10
 * - Left_Brake_CMD (0x98FF0D09): Brake commands (push/release)
 * 
 * Handles:
 * - Message reception and processing (PC commands and heartbeat)
 * - Periodic heartbeat transmission (MCU signals availability)
 * - Periodic telemetry transmission (actuator state to PC)
 * - System health monitoring (communication timeout detection)
 * - Status LED indication
 */
void Business_Loop(void) 
{
    uint32_t current_tick = HAL_GetTick();

    /* Process all pending CAN messages (PC heartbeat + commands) */
    ProcessReceivedMessage();

    /* Send MCU heartbeat at fixed 50ms interval (signals MCU availability to PC) */
    if (current_tick - last_heartbeat_tick >= HEARTBEAT_INTERVAL_MS) {
        SendHeartbeat();
        last_heartbeat_tick = current_tick;
    }
    
    /* Send telemetry at fixed 100ms interval (actuator state to PC) */
    if (current_tick - last_telemetry_tick >= TELEMETRY_INTERVAL_MS) {
        SendTelemetry();
        last_telemetry_tick = current_tick;
    }
    
    /* Update system health status (monitor PC communication) */
    UpdateSystemHealth();
    
    /* Update status LED */
    UpdateStatusLED();
}

/**
 * @brief Set node ID
 * 
 * @param id Node identifier (0-255)
 */
void Controller_SetNodeID(uint8_t id)
{
    node_id = id;
}

/**
 * @brief Get current node ID
 * 
 * @return Current node identifier
 */
uint8_t Controller_GetNodeID(void)
{
    return node_id;
}

/**
 * @brief Set node health status manually
 * 
 * @param health Health status (use AUTOMATE_HEART_BEAT_MSG_HEALTH_* constants)
 */
void Controller_SetHealth(uint8_t health)
{
    if (automate_heart_beat_msg_health_is_in_range(health)) {
        node_health = health;
    }
}

/**
 * @brief Get current node health status
 * 
 * @return Current health status
 */
uint8_t Controller_GetHealth(void)
{
    return node_health;
}

/**
 * @brief Force immediate heartbeat transmission
 * 
 * Useful for testing or when immediate response is needed.
 */
void Controller_SendHeartbeatNow(void)
{
    SendHeartbeat();
    last_heartbeat_tick = HAL_GetTick();
}

/**
 * @brief Force immediate telemetry transmission
 * 
 * Useful for testing or when immediate response is needed.
 */
void Controller_SendTelemetryNow(void)
{
    SendTelemetry();
    last_telemetry_tick = HAL_GetTick();
}