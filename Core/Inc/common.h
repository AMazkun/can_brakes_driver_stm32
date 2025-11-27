#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "stm32g4xx_hal.h"


/* ============================================================================
 * Application State
 * ============================================================================ */

typedef enum {
    BRAKE_STATE_RELEASED = 0,
    BRAKE_STATE_RELEASING,
    BRAKE_STATE_PUSHED,
    BRAKE_STATE_PUSHING,
    BRAKE_STATE_STOPPED
} BrakeState_t;

typedef struct {
    BrakeState_t state;
    uint16_t current_position;      /* ADC value 0-4095 */
    uint16_t target_position;
    uint8_t msg_id_counter;
    uint32_t msg_count;
    uint32_t last_heartbeat_time;
    uint32_t last_telemetry_time;
    uint32_t last_adc_time;
    uint32_t operation_start_time;
    bool command_received;
} AppState_t;

extern AppState_t app_state;
extern ADC_HandleTypeDef hadc1;
extern FDCAN_HandleTypeDef hfdcan1;
extern TIM_HandleTypeDef htim1;


uint32_t GetTick(void);