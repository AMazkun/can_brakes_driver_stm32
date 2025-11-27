/**
 * @file left_brake.h
 * @brief Left brake actuator control interface
 * 
 * Provides high-level control for brake actuator with position feedback
 */

#ifndef LEFT_BRAKE_H
#define LEFT_BRAKE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Public Function Prototypes
 * ============================================================================ */

/**
 * @brief Initialize brake system
 * 
 * Must be called once during system initialization after peripherals are configured.
 * - Calibrates ADC
 * - Reads initial position
 * - Determines initial state
 * - Ensures motor is stopped
 * 
 * Call sequence:
 * 1. HAL_Init()
 * 2. MX_GPIO_Init(), MX_ADC_Init(), MX_TIM_Init()
 * 3. Brake_Init() ← Call this
 */
void Brake_Init(void);

/**
 * @brief Update current position reading from ADC
 * 
 * Reads potentiometer value and validates it. Should be called periodically
 * (recommended: every 10ms) from main loop or timer interrupt.
 * 
 * Handles:
 * - ADC reading
 * - Position validation
 * - Error detection and counting
 * - Automatic error state entry on repeated failures
 * 
 * Example:
 * @code
 * // In main loop or 10ms timer
 * Brake_UpdatePosition();
 * @endcode
 */
void Brake_UpdatePosition(void);

/**
 * @brief Process brake command received from CAN
 * 
 * Initiates push or release operation based on command.
 * Ignores duplicate commands (already pushing/releasing).
 * 
 * @param brake_state Command state:
 *        - AUTOMATE_LEFT_BRAKE_CMD_BRAKE_STATE_PUSH_CHOICE (0)
 *        - AUTOMATE_LEFT_BRAKE_CMD_BRAKE_STATE_RELEASE_CHOICE (1)
 * 
 * @note Command is ignored if system is in error state
 * 
 * Example:
 * @code
 * struct automate_left_brake_cmd_t cmd;
 * if (automate_left_brake_cmd_unpack(&cmd, can_data, 8) == 0) {
 *     Brake_ProcessCommand(cmd.brake_state);
 * }
 * @endcode
 */
void Brake_ProcessCommand(uint8_t brake_state);

/**
 * @brief Update brake state machine
 * 
 * Executes current state logic and performs transitions.
 * Call this periodically (recommended: every 10-50ms) from main loop.
 * 
 * Handles:
 * - State transitions based on position
 * - Motor control (PWM and direction)
 * - Operation timeout detection
 * - Automatic motor stop on completion
 * 
 * State machine flow:
 * RELEASED → (push cmd) → PUSHING → PUSHED
 *                             ↓
 * RELEASING ← (release cmd) ← PUSHED
 *     ↓
 * RELEASED
 * 
 * Example:
 * @code
 * while (1) {
 *     Brake_UpdatePosition();  // Update position first
 *     Brake_Update();          // Then update state machine
 *     HAL_Delay(10);
 * }
 * @endcode
 */
void Brake_Update(void);

/**
 * @brief Get estimated time remaining for current operation
 * 
 * Provides dynamic estimate based on current progress.
 * Returns 0 if not currently in motion.
 * 
 * @return Time remaining in milliseconds (0-5000)
 * 
 * @note Estimate becomes more accurate as operation progresses
 */
uint16_t Brake_GetTimeToEnd(void);

/**
 * @brief Get current position from ADC
 * 
 * @return ADC value (typically 0-4095 for 12-bit ADC)
 *         - ~200: Fully released
 *         - ~3800: Fully pushed
 */
uint16_t Brake_GetPosition(void);

/**
 * @brief Get position as percentage
 * 
 * Converts raw ADC value to percentage where:
 * - 0% = Fully released
 * - 100% = Fully pushed
 * 
 * @return Position percentage (0-100)
 * 
 * Example:
 * @code
 * uint8_t percent = Brake_GetPositionPercent();
 * printf("Brake position: %d%%\n", percent);
 * @endcode
 */
uint8_t Brake_GetPositionPercent(void);

/**
 * @brief Emergency stop motor immediately
 * 
 * Halts motor and enters STOPPED state regardless of current position.
 * Use in emergency situations or system shutdown.
 * 
 * @warning Motor stops immediately without controlled deceleration
 * 
 * Example:
 * @code
 * if (emergency_button_pressed) {
 *     Brake_EmergencyStop();
 * }
 * @endcode
 */
void Brake_EmergencyStop(void);

/**
 * @brief Clear error state and attempt recovery
 * 
 * Resets error counters and attempts to determine valid state
 * based on current position.
 * 
 * @return true if recovery successful (valid position found)
 * @return false if still in error (invalid position)
 * 
 * Example:
 * @code
 * if (Brake_HasError()) {
 *     if (Brake_ClearError()) {
 *         // Recovery successful
 *     } else {
 *         // Still in error - may need hardware check
 *     }
 * }
 * @endcode
 */
bool Brake_ClearError(void);

/**
 * @brief Check if brake system has detected an error
 * 
 * Returns true if:
 * - Too many consecutive invalid position readings
 * - Operation timeout occurred
 * - System is in STOPPED state due to error
 * 
 * @return true if error detected, false if operating normally
 */
bool Brake_HasError(void);

#ifdef __cplusplus
}
#endif

#endif /* LEFT_BRAKE_H */