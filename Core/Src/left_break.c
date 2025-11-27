/**
 * @file left_brake.c
 * @brief Left brake actuator control driver
 * 
 * Controls brake actuator via BTN7971B motor driver with position feedback
 * from potentiometer. Implements state machine for push/release operations.
 */

#include "common.h"
#include "left_break.h"
#include "automate.h"
#include "main.h"

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/* Position thresholds (ADC 12-bit: 0-4095) */
#define POSITION_RELEASED           200     /* Fully released position */
#define POSITION_PUSHED             3800    /* Fully pushed position */
#define POSITION_TOLERANCE          100     /* Position detection tolerance */

/* Motor control */
#define MOTOR_DUTY_PUSH             80      /* 80% duty cycle for pushing */
#define MOTOR_DUTY_RELEASE          80      /* 80% duty cycle for releasing */

/* Timing */
#define ESTIMATED_PUSH_TIME_MS      2000    /* Estimated time to push (2 sec) */
#define ESTIMATED_RELEASE_TIME_MS   2000    /* Estimated time to release (2 sec) */
#define POSITION_TIMEOUT_MS         5000    /* Max time for operation (5 sec) */

/* Safety */
#define MIN_VALID_POSITION          50      /* Minimum valid ADC reading */
#define MAX_VALID_POSITION          4000    /* Maximum valid ADC reading */

/* ============================================================================
 * Private Variables
 * ============================================================================ */

/* Current position from ADC */
static uint16_t current_position = 0;

/* Operation timing */
static uint32_t operation_start_tick = 0;
static uint32_t estimated_operation_time_ms = ESTIMATED_PUSH_TIME_MS;

/* Error tracking */
static uint8_t position_error_count = 0;
#define MAX_POSITION_ERRORS 10

/* ============================================================================
 * Private Function Prototypes
 * ============================================================================ */

static void Motor_SetDirection(bool push);
static void Motor_SetPWM(uint8_t duty_percent);
static void Motor_Stop(void);
static uint16_t ADC_ReadPosition(void);
static bool IsPositionValid(uint16_t position);
static void UpdateOperationEstimate(void);

/* ============================================================================
 * Motor Control Functions (Private)
 * ============================================================================ */

/**
 * @brief Set motor direction via INH pin
 * 
 * BTN7971B control logic:
 * - Push (forward): INH=HIGH, PWM on IN pin
 * - Release (backward): INH=LOW, PWM on IN pin (inverted logic)
 * 
 * @param push true for push direction, false for release
 */
static void Motor_SetDirection(bool push)
{
    if (push) {
        HAL_GPIO_WritePin(MOTOR_INH_GPIO_Port, MOTOR_INH_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(MOTOR_INH_GPIO_Port, MOTOR_INH_Pin, GPIO_PIN_RESET);
    }
}

/**
 * @brief Set motor PWM duty cycle
 * 
 * @param duty_percent Duty cycle percentage (0-100)
 */
static void Motor_SetPWM(uint8_t duty_percent)
{
    /* Clamp to valid range */
    if (duty_percent > 100) {
        duty_percent = 100;
    }
    
    /* Calculate compare value based on timer auto-reload */
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
    uint32_t ccr = (arr * duty_percent) / 100;
    
    /* Set PWM duty cycle */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr);
}

/**
 * @brief Stop motor immediately
 */
static void Motor_Stop(void)
{
    /* Set PWM to 0% */
    Motor_SetPWM(0);
    
    /* Disable motor driver */
    HAL_GPIO_WritePin(MOTOR_INH_GPIO_Port, MOTOR_INH_Pin, GPIO_PIN_RESET);
}

/* ============================================================================
 * Position Reading Functions (Private)
 * ============================================================================ */

/**
 * @brief Read current position from ADC
 * 
 * @return ADC value (0-4095)
 */
static uint16_t ADC_ReadPosition(void)
{
    /* Start conversion */
    HAL_ADC_Start(&hadc1);
    
    /* Wait for conversion with timeout */
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
        uint16_t value = (uint16_t)HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
        return value;
    }
    
    /* Conversion timeout - return last known position */
    HAL_ADC_Stop(&hadc1);
    return current_position;
}

/**
 * @brief Validate position reading
 * 
 * @param position ADC reading to validate
 * @return true if position is in valid range
 */
static bool IsPositionValid(uint16_t position)
{
    return (position >= MIN_VALID_POSITION && position <= MAX_VALID_POSITION);
}

/**
 * @brief Update estimated operation time based on position
 * 
 * Dynamically adjusts time estimate based on current progress
 */
static void UpdateOperationEstimate(void)
{
    uint32_t elapsed = HAL_GetTick() - operation_start_tick;
    uint16_t start_pos, target_pos;
    int32_t distance_total, distance_remaining;
    
    /* Determine start and target based on state */
    if (app_state.state == BRAKE_STATE_PUSHING) {
        start_pos = POSITION_RELEASED;
        target_pos = POSITION_PUSHED;
    } else if (app_state.state == BRAKE_STATE_RELEASING) {
        start_pos = POSITION_PUSHED;
        target_pos = POSITION_RELEASED;
    } else {
        estimated_operation_time_ms = 0;
        return;
    }
    
    /* Calculate distances */
    distance_total = (int32_t)target_pos - (int32_t)start_pos;
    distance_remaining = (int32_t)target_pos - (int32_t)current_position;
    
    /* Avoid division by zero */
    if (distance_total == 0) {
        estimated_operation_time_ms = 0;
        return;
    }
    
    /* Calculate remaining time based on progress */
    if (elapsed > 0) {
        int32_t distance_traveled = distance_total - distance_remaining;
        if (distance_traveled > 0) {
            /* Extrapolate based on current speed */
            uint32_t time_per_unit = elapsed / distance_traveled;
            estimated_operation_time_ms = time_per_unit * distance_remaining;
        } else {
            /* No progress yet - use default estimate */
            estimated_operation_time_ms = (app_state.state == BRAKE_STATE_PUSHING) ?
                                         ESTIMATED_PUSH_TIME_MS : ESTIMATED_RELEASE_TIME_MS;
        }
    }
}

/* ============================================================================
 * Public Functions
 * ============================================================================ */

/**
 * @brief Initialize brake system
 * 
 * Must be called once during system initialization
 */
void Brake_Init(void)
{
    /* Initialize variables */
    current_position = 0;
    operation_start_tick = 0;
    estimated_operation_time_ms = ESTIMATED_PUSH_TIME_MS;
    position_error_count = 0;
    
    /* Ensure motor is stopped */
    Motor_Stop();
    
    /* Perform initial ADC calibration */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    
    /* Read initial position */
    current_position = ADC_ReadPosition();
    
    /* Determine initial state based on position */
    if (current_position <= (POSITION_RELEASED + POSITION_TOLERANCE)) {
        app_state.state = BRAKE_STATE_RELEASED;
        app_state.target_position = POSITION_RELEASED;
    } else if (current_position >= (POSITION_PUSHED - POSITION_TOLERANCE)) {
        app_state.state = BRAKE_STATE_PUSHED;
        app_state.target_position = POSITION_PUSHED;
    } else {
        /* In between - assume released */
        app_state.state = BRAKE_STATE_RELEASED;
        app_state.target_position = POSITION_RELEASED;
    }
}

/**
 * @brief Update current position reading
 * 
 * Call this periodically (e.g., every 10ms) from main loop
 */
void Brake_UpdatePosition(void)
{
    uint16_t new_position = ADC_ReadPosition();
    
    /* Validate reading */
    if (IsPositionValid(new_position)) {
        current_position = new_position;
        app_state.current_position = new_position;
        position_error_count = 0;
    } else {
        /* Invalid reading - increment error counter */
        position_error_count++;
        
        /* If too many errors, enter error state */
        if (position_error_count >= MAX_POSITION_ERRORS) {
            app_state.state = BRAKE_STATE_STOPPED;
            Motor_Stop();
        }
    }
}

/**
 * @brief Process brake command from CAN
 * 
 * @param brake_state Command state (PUSH or RELEASE)
 */
void Brake_ProcessCommand(uint8_t brake_state)
{
    /* Ignore commands if in error state */
    if (app_state.state == BRAKE_STATE_STOPPED && position_error_count >= MAX_POSITION_ERRORS) {
        return;
    }
    
    if (brake_state == AUTOMATE_LEFT_BRAKE_CMD_BRAKE_STATE_PUSH_CHOICE) {
        /* Command to push brake */
        if (app_state.state != BRAKE_STATE_PUSHING && 
            app_state.state != BRAKE_STATE_PUSHED) {
            
            app_state.state = BRAKE_STATE_PUSHING;
            app_state.target_position = POSITION_PUSHED;
            operation_start_tick = HAL_GetTick();
            estimated_operation_time_ms = ESTIMATED_PUSH_TIME_MS;
        }
    }
    else if (brake_state == AUTOMATE_LEFT_BRAKE_CMD_BRAKE_STATE_RELEASE_CHOICE) {
        /* Command to release brake */
        if (app_state.state != BRAKE_STATE_RELEASING && 
            app_state.state != BRAKE_STATE_RELEASED) {
            
            app_state.state = BRAKE_STATE_RELEASING;
            app_state.target_position = POSITION_RELEASED;
            operation_start_tick = HAL_GetTick();
            estimated_operation_time_ms = ESTIMATED_RELEASE_TIME_MS;
        }
    }
}

/**
 * @brief Update brake state machine
 * 
 * Call this periodically from main loop to execute state transitions
 */
void Brake_Update(void)
{
    uint16_t pos = current_position;
    uint32_t current_tick = HAL_GetTick();
    
    /* Check for operation timeout */
    if ((app_state.state == BRAKE_STATE_PUSHING || app_state.state == BRAKE_STATE_RELEASING) &&
        (current_tick - operation_start_tick) > POSITION_TIMEOUT_MS) {
        /* Operation timed out - stop motor and enter error state */
        app_state.state = BRAKE_STATE_STOPPED;
        Motor_Stop();
        return;
    }
    
    switch (app_state.state)
    {
        case BRAKE_STATE_PUSHING:
        {
            /* Update time estimate */
            UpdateOperationEstimate();
            
            /* Check if reached target */
            if (pos >= (POSITION_PUSHED - POSITION_TOLERANCE)) {
                app_state.state = BRAKE_STATE_PUSHED;
                Motor_Stop();
            } else {
                /* Continue pushing */
                Motor_SetDirection(true);
                Motor_SetPWM(MOTOR_DUTY_PUSH);
            }
            break;
        }
        
        case BRAKE_STATE_RELEASING:
        {
            /* Update time estimate */
            UpdateOperationEstimate();
            
            /* Check if reached target */
            if (pos <= (POSITION_RELEASED + POSITION_TOLERANCE)) {
                app_state.state = BRAKE_STATE_RELEASED;
                Motor_Stop();
            } else {
                /* Continue releasing */
                Motor_SetDirection(false);
                Motor_SetPWM(MOTOR_DUTY_RELEASE);
            }
            break;
        }
        
        case BRAKE_STATE_PUSHED:
        case BRAKE_STATE_RELEASED:
            /* Target reached - ensure motor is stopped */
            Motor_Stop();
            estimated_operation_time_ms = 0;
            break;
            
        case BRAKE_STATE_STOPPED:
        default:
            /* Error or unknown state - stop motor */
            Motor_Stop();
            estimated_operation_time_ms = 0;
            break;
    }
}

/**
 * @brief Get estimated time to end of operation
 * 
 * @return Estimated remaining time in milliseconds
 */
uint16_t Brake_GetTimeToEnd(void)
{
    if (app_state.state == BRAKE_STATE_PUSHING || 
        app_state.state == BRAKE_STATE_RELEASING) {
        
        uint32_t elapsed = HAL_GetTick() - operation_start_tick;
        
        if (elapsed < estimated_operation_time_ms) {
            return (uint16_t)(estimated_operation_time_ms - elapsed);
        }
    }
    
    return 0;
}

/**
 * @brief Get current position
 * 
 * @return Current ADC position value (0-4095)
 */
uint16_t Brake_GetPosition(void)
{
    return current_position;
}

/**
 * @brief Emergency stop - immediately halt motor
 * 
 * Call this in case of emergency or system shutdown
 */
void Brake_EmergencyStop(void)
{
    Motor_Stop();
    app_state.state = BRAKE_STATE_STOPPED;
}

/**
 * @brief Clear error state and reset
 * 
 * Attempts to recover from error state
 * 
 * @return true if reset successful, false if still in error
 */
bool Brake_ClearError(void)
{
    position_error_count = 0;
    
    /* Read current position */
    Brake_UpdatePosition();
    
    /* Determine state based on position */
    if (IsPositionValid(current_position)) {
        if (current_position <= (POSITION_RELEASED + POSITION_TOLERANCE)) {
            app_state.state = BRAKE_STATE_RELEASED;
            app_state.target_position = POSITION_RELEASED;
        } else if (current_position >= (POSITION_PUSHED - POSITION_TOLERANCE)) {
            app_state.state = BRAKE_STATE_PUSHED;
            app_state.target_position = POSITION_PUSHED;
        } else {
            app_state.state = BRAKE_STATE_RELEASED;
            app_state.target_position = POSITION_RELEASED;
        }
        return true;
    }
    
    return false;
}

/**
 * @brief Check if brake is in error state
 * 
 * @return true if error detected
 */
bool Brake_HasError(void)
{
    return (position_error_count >= MAX_POSITION_ERRORS);
}

/**
 * @brief Get position in percentage (0-100%)
 * 
 * @return Position as percentage where 0% = released, 100% = pushed
 */
uint8_t Brake_GetPositionPercent(void)
{
    if (current_position <= POSITION_RELEASED) {
        return 0;
    }
    if (current_position >= POSITION_PUSHED) {
        return 100;
    }
    
    /* Calculate percentage between released and pushed */
    uint32_t range = POSITION_PUSHED - POSITION_RELEASED;
    uint32_t offset = current_position - POSITION_RELEASED;
    
    return (uint8_t)((offset * 100) / range);
}