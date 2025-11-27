/**
 * @file can.c
 * @brief CAN driver with ring buffer implementation for STM32 FDCAN
 * 
 * Features:
 * - Thread-safe ring buffers for RX and TX
 * - Support for extended 29-bit and standard 11-bit IDs
 * - Non-blocking transmission with automatic retry
 * - Interrupt-driven reception
 */

#include <string.h>
#include "common.h"
#include "can.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

/* Ring buffer size (must be power of 2 for efficient modulo operation) */
#define CAN_RX_BUFFER_SIZE          8
#define CAN_TX_BUFFER_SIZE          8

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Ring buffer structure for CAN messages
 */
typedef struct {
    CAN_Message_t buffer[CAN_TX_BUFFER_SIZE];  /* Message storage */
    volatile uint8_t head;                      /* Write index */
    volatile uint8_t tail;                      /* Read index */
    volatile uint8_t count;                     /* Number of messages in buffer */
} CAN_RingBuffer_t;

/* ============================================================================
 * Private Variables
 * ============================================================================ */

/* Separate ring buffers for RX and TX */
static CAN_RingBuffer_t can_rx_buffer = {0};
static CAN_RingBuffer_t can_tx_buffer = {0};

/* ============================================================================
 * Private Function Prototypes
 * ============================================================================ */

static bool RingBuffer_Put(CAN_RingBuffer_t *buffer, const CAN_Message_t *msg, uint8_t buffer_size);
static bool RingBuffer_Get(CAN_RingBuffer_t *buffer, CAN_Message_t *msg, uint8_t buffer_size);
static bool RingBuffer_IsFull(const CAN_RingBuffer_t *buffer, uint8_t buffer_size);
static bool RingBuffer_IsEmpty(const CAN_RingBuffer_t *buffer);
static uint8_t RingBuffer_GetCount(const CAN_RingBuffer_t *buffer);

/* ============================================================================
 * Public Functions
 * ============================================================================ */

/**
 * @brief Initialize CAN driver
 * 
 * Call this once during system initialization after HAL_FDCAN_Init()
 */
void CAN_Driver_Init(void)
{
    /* Clear ring buffers */
    memset(&can_rx_buffer, 0, sizeof(can_rx_buffer));
    memset(&can_tx_buffer, 0, sizeof(can_tx_buffer));
}

/**
 * @brief Queue a CAN message for transmission
 * 
 * @param id CAN identifier (11-bit standard or 29-bit extended)
 * @param data Pointer to message data (up to 8 bytes)
 * @param len Data length (0-8 bytes)
 * @return true if message queued successfully, false if buffer full or invalid parameters
 */
bool CAN_Driver_Send(uint32_t id, const uint8_t *data, uint8_t len)
{
    /* Validate parameters */
    if (data == NULL || len > 8) {
        return false;
    }
    
    /* Create message */
    CAN_Message_t msg;
    msg.id = id;
    msg.len = len;
    msg.is_extended = true;  /* Default to extended ID (29-bit) */
    
    /* Copy data (only up to len bytes) */
    memcpy(msg.data, data, len);
    
    /* Clear unused bytes for safety */
    if (len < 8) {
        memset(&msg.data[len], 0, 8 - len);
    }
    
    /* Add to TX ring buffer */
    return RingBuffer_Put(&can_tx_buffer, &msg, CAN_TX_BUFFER_SIZE);
}

/**
 * @brief Process pending CAN transmissions
 * 
 * Call this periodically from main loop to send queued messages.
 * Messages are transmitted in FIFO order.
 * 
 * @note Non-blocking - returns immediately even if transmission fails
 */
void CAN_Driver_Transmit(void)
{
    CAN_Message_t msg;
    
    /* Process all messages in TX buffer */
    while (RingBuffer_Get(&can_tx_buffer, &msg, CAN_TX_BUFFER_SIZE)) {
        FDCAN_TxHeaderTypeDef tx_header;
        
        /* Configure TX header */
        tx_header.Identifier = msg.id;
        tx_header.IdType = msg.is_extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
        tx_header.TxFrameType = FDCAN_DATA_FRAME;
        tx_header.DataLength = msg.len << 16; /* Convert DLC to FDCAN format */
        tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        tx_header.BitRateSwitch = FDCAN_BRS_OFF;
        tx_header.FDFormat = FDCAN_CLASSIC_CAN;
        tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
        tx_header.MessageMarker = 0;
        
        /* Attempt transmission */
        if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx_header, msg.data) != HAL_OK) {
            /* TX FIFO full - put message back and retry later */
            RingBuffer_Put(&can_tx_buffer, &msg, CAN_TX_BUFFER_SIZE);
            break;  /* Stop processing to avoid infinite loop */
        }
    }
}

/**
 * @brief Get a received CAN message from the queue
 * 
 * @param msg Pointer to message structure to fill
 * @return true if message retrieved, false if no messages available
 */
bool CAN_Driver_Get(CAN_Message_t *msg)
{
    if (msg == NULL) {
        return false;
    }
    
    return RingBuffer_Get(&can_rx_buffer, msg, CAN_RX_BUFFER_SIZE);
}

/**
 * @brief Get number of messages waiting in RX buffer
 * 
 * @return Number of unread messages (0-CAN_RX_BUFFER_SIZE)
 */
uint8_t CAN_Driver_GetRxCount(void)
{
    return RingBuffer_GetCount(&can_rx_buffer);
}

/**
 * @brief Get number of messages waiting in TX buffer
 * 
 * @return Number of pending transmissions (0-CAN_TX_BUFFER_SIZE)
 */
uint8_t CAN_Driver_GetTxCount(void)
{
    return RingBuffer_GetCount(&can_tx_buffer);
}

/**
 * @brief Check if RX buffer has messages
 * 
 * @return true if messages available, false if empty
 */
bool CAN_Driver_HasMessage(void)
{
    return !RingBuffer_IsEmpty(&can_rx_buffer);
}

/**
 * @brief Clear all pending TX messages
 * 
 * Useful for emergency stop or reset scenarios
 */
void CAN_Driver_ClearTxBuffer(void)
{
    __disable_irq();
    can_tx_buffer.head = 0;
    can_tx_buffer.tail = 0;
    can_tx_buffer.count = 0;
    __enable_irq();
}

/**
 * @brief Clear all received messages
 */
void CAN_Driver_ClearRxBuffer(void)
{
    __disable_irq();
    can_rx_buffer.head = 0;
    can_rx_buffer.tail = 0;
    can_rx_buffer.count = 0;
    __enable_irq();
}

/* ============================================================================
 * Interrupt Callbacks
 * ============================================================================ */

/**
 * @brief Internal RX callback handler
 * 
 * @param hfdcan Pointer to FDCAN handle
 */
static void CAN_Driver_RxCallback(FDCAN_HandleTypeDef *hfdcan)
{
    FDCAN_RxHeaderTypeDef rx_header;
    uint8_t rx_data[8];
    
    /* Get message from FDCAN peripheral */
    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK) {
        CAN_Message_t msg;
        
        /* Extract message information */
        msg.id = rx_header.Identifier;
        msg.is_extended = (rx_header.IdType == FDCAN_EXTENDED_ID);
        msg.len = (uint8_t)(rx_header.DataLength >> 16); /* Extract DLC */
        
        /* Limit DLC to valid range */
        if (msg.len > 8) {
            msg.len = 8;
        }
        
        /* Copy data */
        memcpy(msg.data, rx_data, msg.len);
        
        /* Clear unused bytes */
        if (msg.len < 8) {
            memset(&msg.data[msg.len], 0, 8 - msg.len);
        }
        
        /* Add to RX buffer (if buffer full, message is dropped) */
        RingBuffer_Put(&can_rx_buffer, &msg, CAN_RX_BUFFER_SIZE);
    }
}

/**
 * @brief HAL FDCAN RX FIFO 0 callback
 * 
 * Called by HAL when new message arrives in RX FIFO 0
 * 
 * @param hfdcan Pointer to FDCAN handle
 * @param RxFifo0ITs Interrupt flags
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    /* Check if new message interrupt */
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0) {
        CAN_Driver_RxCallback(hfdcan);
    }
    
    /* Check for FIFO full warning */
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_FULL) != 0) {
        /* FIFO is full - messages may be lost */
        /* Could set error flag here */
    }
    
    /* Check for message lost */
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0) {
        /* Messages were lost due to FIFO overflow */
        /* Could increment error counter here */
    }
}

/* ============================================================================
 * Ring Buffer Implementation
 * ============================================================================ */

/**
 * @brief Add message to ring buffer
 * 
 * @param buffer Pointer to ring buffer
 * @param msg Pointer to message to add
 * @param buffer_size Maximum size of buffer
 * @return true if added successfully, false if buffer full
 */
static bool RingBuffer_Put(CAN_RingBuffer_t *buffer, const CAN_Message_t *msg, uint8_t buffer_size)
{
    /* Check if buffer is full */
    if (RingBuffer_IsFull(buffer, buffer_size)) {
        return false;
    }
    
    /* Add message to buffer */
    buffer->buffer[buffer->head] = *msg;
    
    /* Update head pointer with wrap-around */
    buffer->head = (buffer->head + 1) % buffer_size;
    
    /* Increment count atomically */
    __disable_irq();
    buffer->count++;
    __enable_irq();
    
    return true;
}

/**
 * @brief Get message from ring buffer
 * 
 * @param buffer Pointer to ring buffer
 * @param msg Pointer to message structure to fill
 * @param buffer_size Maximum size of buffer
 * @return true if message retrieved, false if buffer empty
 */
static bool RingBuffer_Get(CAN_RingBuffer_t *buffer, CAN_Message_t *msg, uint8_t buffer_size)
{
    /* Check if buffer is empty */
    if (RingBuffer_IsEmpty(buffer)) {
        return false;
    }
    
    /* Get message from buffer */
    *msg = buffer->buffer[buffer->tail];
    
    /* Update tail pointer with wrap-around */
    buffer->tail = (buffer->tail + 1) % buffer_size;
    
    /* Decrement count atomically */
    __disable_irq();
    buffer->count--;
    __enable_irq();
    
    return true;
}

/**
 * @brief Check if ring buffer is full
 * 
 * @param buffer Pointer to ring buffer
 * @param buffer_size Maximum size of buffer
 * @return true if full, false otherwise
 */
static bool RingBuffer_IsFull(const CAN_RingBuffer_t *buffer, uint8_t buffer_size)
{
    return (buffer->count >= buffer_size);
}

/**
 * @brief Check if ring buffer is empty
 * 
 * @param buffer Pointer to ring buffer
 * @return true if empty, false otherwise
 */
static bool RingBuffer_IsEmpty(const CAN_RingBuffer_t *buffer)
{
    return (buffer->count == 0);
}

/**
 * @brief Get number of messages in buffer
 * 
 * @param buffer Pointer to ring buffer
 * @return Number of messages (0-buffer_size)
 */
static uint8_t RingBuffer_GetCount(const CAN_RingBuffer_t *buffer)
{
    uint8_t count;
    
    /* Read count atomically */
    __disable_irq();
    count = buffer->count;
    __enable_irq();
    
    return count;
}