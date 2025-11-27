/**
 * @file can.h
 * @brief CAN driver interface for STM32 FDCAN peripheral
 * 
 * Provides high-level CAN communication with ring buffer management
 */

#ifndef CAN_H
#define CAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief CAN message structure
 * 
 * Represents a single CAN frame with identifier, data, and metadata
 */
typedef struct {
    uint32_t id;            /**< CAN identifier (11-bit standard or 29-bit extended) */
    uint8_t data[8];        /**< Message data payload (0-8 bytes) */
    uint8_t len;            /**< Data length (0-8) */
    bool is_extended;       /**< true for 29-bit extended ID, false for 11-bit standard */
} CAN_Message_t;

/* ============================================================================
 * Public Function Prototypes
 * ============================================================================ */

/**
 * @brief Initialize CAN driver
 * 
 * Must be called once during system initialization after HAL_FDCAN_Init()
 * and before any other CAN driver functions.
 * Initializes ring buffers and internal state.
 */
void CAN_Driver_Init(void);

/**
 * @brief Queue a CAN message for transmission
 * 
 * Adds message to the TX ring buffer. Message will be transmitted
 * when CAN_Driver_Transmit() is called.
 * 
 * @param id CAN identifier (11 or 29 bits depending on extended flag)
 * @param data Pointer to message data (up to 8 bytes)
 * @param len Data length in bytes (0-8)
 * 
 * @return true if message queued successfully
 * @return false if buffer full or invalid parameters
 * 
 * @note Messages are transmitted in FIFO order
 * @note This function is non-blocking
 * 
 * Example:
 * @code
 * uint8_t data[8] = {0x01, 0x02, 0x03};
 * if (!CAN_Driver_Send(0x123, data, 3)) {
 *     // Handle buffer full
 * }
 * @endcode
 */
bool CAN_Driver_Send(uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief Process pending CAN transmissions
 * 
 * Attempts to send all queued messages from TX buffer to the CAN peripheral.
 * Should be called periodically from main loop or timer interrupt.
 * 
 * @note Non-blocking - returns immediately if peripheral TX FIFO is full
 * @note Messages that cannot be sent are kept in buffer for next attempt
 * 
 * Example:
 * @code
 * while (1) {
 *     CAN_Driver_Transmit();  // Send pending messages
 *     // Other tasks...
 * }
 * @endcode
 */
void CAN_Driver_Transmit(void);

/**
 * @brief Get next received CAN message from queue
 * 
 * Retrieves oldest message from RX ring buffer (FIFO order).
 * 
 * @param msg Pointer to message structure to fill with received data
 * 
 * @return true if message retrieved successfully
 * @return false if no messages available or msg is NULL
 * 
 * Example:
 * @code
 * CAN_Message_t msg;
 * while (CAN_Driver_Get(&msg)) {
 *     // Process message
 *     printf("ID: 0x%X, Data: %d bytes\n", msg.id, msg.len);
 * }
 * @endcode
 */
bool CAN_Driver_Get(CAN_Message_t *msg);

/**
 * @brief Get number of received messages waiting in buffer
 * 
 * @return Number of unread messages (0-8)
 */
uint8_t CAN_Driver_GetRxCount(void);

/**
 * @brief Get number of messages pending transmission
 * 
 * @return Number of messages in TX buffer (0-8)
 */
uint8_t CAN_Driver_GetTxCount(void);

/**
 * @brief Check if any messages are available
 * 
 * @return true if at least one message in RX buffer
 * @return false if RX buffer is empty
 * 
 * @note More efficient than CAN_Driver_GetRxCount() > 0
 */
bool CAN_Driver_HasMessage(void);

/**
 * @brief Clear all pending transmissions
 * 
 * Discards all messages waiting in TX buffer.
 * Useful for emergency stop or system reset.
 * 
 * @warning Messages already submitted to hardware FIFO will still be sent
 */
void CAN_Driver_ClearTxBuffer(void);

/**
 * @brief Clear all received messages
 * 
 * Discards all unread messages in RX buffer.
 */
void CAN_Driver_ClearRxBuffer(void);

/**
 * @brief HAL FDCAN RX FIFO 0 callback
 * 
 * This function is called by HAL when a new message arrives.
 * It should be registered with the HAL FDCAN driver.
 * 
 * @param hfdcan Pointer to FDCAN handle
 * @param RxFifo0ITs Interrupt flags indicating event type
 * 
 * @note This is a weak function that can be overridden by application
 * @note Do not call this function directly
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs);

#ifdef __cplusplus
}
#endif

#endif /* CAN_H */