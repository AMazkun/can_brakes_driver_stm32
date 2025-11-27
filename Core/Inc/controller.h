/**
 * @file controller.h
 * @brief Main business logic controller interface
 * 
 * Implements bidirectional CAN communication protocol:
 * 
 * Message Flow:
 * ============
 * 
 * MCU → PC (Transmitted by this controller):
 * - Heart_Beat_MSG (CAN ID: 0x98FF0D00, Node_id: 0xF0)
 *   * Period: 50ms
 *   * Purpose: Signal MCU availability and health status
 * 
 * - Left_Brake_MSG (CAN ID: 0x98FF0D0A)
 *   * Period: 100ms
 *   * Purpose: Report actual brake actuator state
 * 
 * PC → MCU (Received and processed):
 * - Heart_Beat_MSG (CAN ID: 0x98FF0D00, Node_id: 0x10)
 *   * Expected period: 50ms
 *   * Purpose: Monitor PC availability
 *   * Timeout: 200ms (4 missed messages)
 * 
 * - Left_Brake_CMD (CAN ID: 0x98FF0D09)
 *   * Purpose: Receive brake control commands
 *   * Brake_State: 0 = release, 1 = push
 * 
 * Node Identification:
 * ===================
 * - MCU Node_id: 0xF0 (fixed, used in outgoing Heart_Beat_MSG)
 * - PC Node_id: 0x10 (expected in incoming Heart_Beat_MSG)
 */

#ifndef CONTROLLER_H
#define CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Public Function Prototypes
 * ============================================================================ */

/**
 * @brief Initialize controller subsystem
 * 
 * Must be called once during system initialization before calling BusinessLoop().
 * Initializes all controller state variables and timing.
 */
void Controller_Init(void);

/**
 * @brief Main business logic loop
 * 
 * Call this function repeatedly from the main loop (typically in while(1)).
 * Handles all periodic tasks:
 * - CAN message reception and processing
 * - Heartbeat transmission (every 50ms)
 * - Telemetry transmission (every 100ms)
 * - System health monitoring
 * - Status LED control
 * 
 * @note This is a non-blocking function that returns immediately
 */
void Business_Loop(void);

/**
 * @brief Set node identifier
 * 
 * Changes the node ID used in heartbeat messages.
 * 
 * @param id Node identifier (0-255)
 */
void Controller_SetNodeID(uint8_t id);

/**
 * @brief Get current node identifier
 * 
 * @return Current node ID
 */
uint8_t Controller_GetNodeID(void);

/**
 * @brief Set system health status manually
 * 
 * Allows external code to override health status (e.g., during fault conditions).
 * Valid values are defined in automate.h:
 * - AUTOMATE_HEART_BEAT_MSG_HEALTH_OFF_CHOICE (0)
 * - AUTOMATE_HEART_BEAT_MSG_HEALTH_ON_CHOICE (1)
 * - AUTOMATE_HEART_BEAT_MSG_HEALTH_INIT_CHOICE (2)
 * - AUTOMATE_HEART_BEAT_MSG_HEALTH_WARNING_CHOICE (3)
 * - AUTOMATE_HEART_BEAT_MSG_HEALTH_FAILURE_CHOICE (4)
 * - AUTOMATE_HEART_BEAT_MSG_HEALTH_CRITICAL_FAILURE_CHOICE (5)
 * 
 * @param health Health status code (0-5)
 */
void Controller_SetHealth(uint8_t health);

/**
 * @brief Get current system health status
 * 
 * @return Current health status (0-5)
 */
uint8_t Controller_GetHealth(void);

/**
 * @brief Force immediate heartbeat transmission
 * 
 * Bypasses the periodic timer and sends a heartbeat immediately.
 * Useful for testing or critical status updates.
 */
void Controller_SendHeartbeatNow(void);

/**
 * @brief Force immediate telemetry transmission
 * 
 * Bypasses the periodic timer and sends telemetry immediately.
 * Useful for testing or on-demand status reporting.
 */
void Controller_SendTelemetryNow(void);

#ifdef __cplusplus
}
#endif

#endif /* CONTROLLER_H */