/**
 *
 * @copyright &copy; 2010 - 2022, Fraunhofer-Gesellschaft zur Foerderung der angewandten Forschung e.V.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * We kindly request you to use one or more of the following phrases to refer to
 * foxBMS in your hardware, software, documentation or advertising materials:
 *
 * - &Prime;This product uses parts of foxBMS&reg;&Prime;
 * - &Prime;This product includes parts of foxBMS&reg;&Prime;
 * - &Prime;This product is derived from foxBMS&reg;&Prime;
 *
 */

/**
 * @file    can.c
 * @author  foxBMS Team
 * @date    2019-12-04 (date of creation)
 * @updated 2022-10-27 (date of last update)
 * @version v1.4.1
 * @ingroup DRIVERS
 * @prefix  CAN
 *
 * @brief   Driver for the CAN module
 *
 * @details Implementation of the CAN Interrupts, initialization, buffers,
 *          receive and transmit interfaces.
 */

/*========== Includes =======================================================*/
#include "can.h"

#include "version_cfg.h"

#include "HL_het.h"
#include "HL_reg_system.h"

#include "bender_iso165c.h"
#include "can_helper.h"
#include "database.h"
#include "diag.h"
#include "foxmath.h"
#include "ftask.h"
#include "pex.h"

/*========== Macros and Definitions =========================================*/
/** lower limit of timestamp counts for a valid CAN timing */
#define CAN_TIMING_LOWER_LIMIT_COUNTS (95u)

/** upper limit of timestamp counts for a valid CAN timing */
#define CAN_TIMING_UPPER_LIMIT_COUNTS (105u)

/** return value of function canGetData if no data was lost during reception */
#define CAN_HAL_RETVAL_NO_DATA_LOST (1u)

/*========== Static Constant and Variable Definitions =======================*/

/** tracks the local state of the can module */
static CAN_STATE_s can_state = {
    .periodicEnable         = false,
    .currentSensorPresent   = {GEN_REPEAT_U(false, GEN_STRIP(BS_NR_OF_STRINGS))},
    .currentSensorCCPresent = {GEN_REPEAT_U(false, GEN_STRIP(BS_NR_OF_STRINGS))},
    .currentSensorECPresent = {GEN_REPEAT_U(false, GEN_STRIP(BS_NR_OF_STRINGS))},
};

/*========== Extern Constant and Variable Definitions =======================*/

/*========== Static Function Prototypes =====================================*/

/**
 * @brief   Called in case of CAN TX interrupt.
 * @param   pNode        CAN interface on which message was sent
 * @param   messageBox   message box on which message was sent
 */
static void CAN_TxInterrupt(canBASE_t *pNode, uint32 messageBox);

/**
 * @brief   Called in case of CAN RX interrupt.
 * @param   pNode        CAN interface on which message was received
 * @param   messageBox   message box on which message was received
 */
static void CAN_RxInterrupt(canBASE_t *pNode, uint32 messageBox);

/**
 * @brief    Handles the processing of messages that are meant to be transmitted.
 * This function looks for the repetition times and the repetition phase of
 * messages that are intended to be sent periodically. If a comparison with
 * an internal counter (i.e., the counter how often this function has been called)
 * states that a transmit is pending, the message is composed by call of CANS_ComposeMessage
 * and transferred to the buffer of the CAN module. If a callback function
 * is declared in configuration, this callback is called after successful transmission.
 * @return   #STD_OK if a CAN transfer was made, #STD_NOT_OK otherwise
 */
static STD_RETURN_TYPE_e CAN_PeriodicTransmit(void);

/**
 * @brief   Checks if the CAN messages come in the specified time window
 * If the (current time stamp) - (previous time stamp) > 96ms and < 104ms,
 * the check is true, false otherwise. The result is reported via flags
 * in the DIAG module.
 */
static void CAN_CheckCanTiming(void);

#if CURRENT_SENSOR_PRESENT == true
/**
 * @brief   Sets flag to indicate current sensor is present.
 *
 * @param command        true if current sensor present, otherwise false
 * @param stringNumber   string addressed
 *
 * @return  none
 */
static void CAN_SetCurrentSensorPresent(bool command, uint8_t stringNumber);

/**
 * @brief   Sets flag to indicate current sensor sends C-C values.
 *
 * @param command        true if coulomb counting message detected, otherwise false
 * @param stringNumber   string addressed
 *
 * @return  none
 */
static void CAN_SetCurrentSensorCcPresent(bool command, uint8_t stringNumber);

/**
 * @brief   Sets flag to indicate current sensor sends C-C values.
 *
 * @param command        true if energy counting message detected, otherwise false
 * @param stringNumber   string addressed
 *
 * @return  none
 */
static void CAN_SetCurrentSensorEcPresent(bool command, uint8_t stringNumber);
#endif /* CURRENT_SENSOR_PRESENT == true */

/** initialize the SPI interface to the CAN transceiver */
static void CAN_InitializeTransceiver(void);

/** checks that the configured message period for Tx messages is valid */
static void CAN_ValidateConfiguredTxMessagePeriod(void);

/*========== Static Function Implementations ================================*/

static void CAN_InitializeTransceiver(void) {
    /** Initialize transceiver for CAN1 */
    PEX_SetPinDirectionOutput(PEX_PORT_EXPANDER2, CAN1_ENABLE_PIN);
    PEX_SetPinDirectionOutput(PEX_PORT_EXPANDER2, CAN1_STANDBY_PIN);
    PEX_SetPin(PEX_PORT_EXPANDER2, CAN1_ENABLE_PIN);
    PEX_SetPin(PEX_PORT_EXPANDER2, CAN1_STANDBY_PIN);

    /** Initialize transceiver for CAN2 */
    PEX_SetPinDirectionOutput(PEX_PORT_EXPANDER2, CAN2_ENABLE_PIN);
    PEX_SetPinDirectionOutput(PEX_PORT_EXPANDER2, CAN2_STANDBY_PIN);
    PEX_SetPin(PEX_PORT_EXPANDER2, CAN2_ENABLE_PIN);
    PEX_SetPin(PEX_PORT_EXPANDER2, CAN2_STANDBY_PIN);
}

static void CAN_ValidateConfiguredTxMessagePeriod(void) {
    for (uint16_t i = 0u; i < can_txLength; i++) {
        if (can_txMessages[i].timing.period == 0u) {
            FAS_ASSERT(FAS_TRAP);
        }
    }
}

/*========== Extern Function Implementations ================================*/

extern void CAN_Initialize(void) {
    CAN_InitializeTransceiver();
    CAN_ValidateConfiguredTxMessagePeriod();
}

extern STD_RETURN_TYPE_e CAN_DataSend(canBASE_t *pNode, uint32_t id, uint8 *pData) {
    FAS_ASSERT(pNode != NULL_PTR);
    FAS_ASSERT(pData != NULL_PTR);
    FAS_ASSERT((pNode == CAN_NODE_1) || (pNode == CAN_NODE_2));
    /* AXIVION Routine Generic-MissingParameterAssert: id: parameter accepts whole range */

    STD_RETURN_TYPE_e result = STD_NOT_OK;

    /**
     *  Parse all TX message boxes until we find a free one,
     *  then use it to send the CAN message.
     *  In the HAL, message box numbers start from 1, not 0.
     */
    for (uint8_t messageBox = 1u; messageBox <= CAN_NR_OF_TX_MESSAGE_BOX; messageBox++) {
        if (canIsTxMessagePending(pNode, messageBox) == 0u) {
            /* id shifted by 18 to use standard frame */
            /* standard frame: bits [28:18] */
            /* extended frame: bits [28:0] */
            /* bit 29 set to 1: to set direction Tx in IF2ARB register */
            canUpdateID(pNode, messageBox, ((id << 18u) | (1u << 29u)));
            canTransmit(pNode, messageBox, pData);
            result = STD_OK;
            break;
        }
    }
    return result;
}

extern void CAN_MainFunction(void) {
    CAN_CheckCanTiming();
    if (can_state.periodicEnable == true) {
        CAN_PeriodicTransmit();
    }
}

static STD_RETURN_TYPE_e CAN_PeriodicTransmit(void) {
    STD_RETURN_TYPE_e retVal     = STD_NOT_OK;
    static uint32_t counterTicks = 0;
    uint8_t data[8]              = {0};

    for (uint16_t i = 0u; i < can_txLength; i++) {
        if (((counterTicks * CAN_TICK_ms) % (can_txMessages[i].timing.period)) == can_txMessages[i].timing.phase) {
            if (can_txMessages[i].callbackFunction != NULL_PTR) {
                can_txMessages[i].callbackFunction(
                    can_txMessages[i].message, data, can_txMessages[i].pMuxId, &can_kShim);
                /* CAN messages are currently discarded if all message boxes
                 * are full. They will not be retransmitted within the next
                 * call of CAN_PeriodicTransmit() */
                CAN_DataSend(can_txMessages[i].canNode, can_txMessages[i].message.id, data);
                retVal = STD_OK;
            }
        }
    }

    counterTicks++;
    return retVal;
}

static void CAN_CheckCanTiming(void) {
    uint32_t current_time;
    DATA_BLOCK_ERRORSTATE_s errorFlagsTab     = {.header.uniqueId = DATA_BLOCK_ID_ERRORSTATE};
    DATA_BLOCK_STATEREQUEST_s staterequestTab = {.header.uniqueId = DATA_BLOCK_ID_STATEREQUEST};
#if CURRENT_SENSOR_PRESENT == true
    DATA_BLOCK_CURRENT_SENSOR_s currentTab = {.header.uniqueId = DATA_BLOCK_ID_CURRENT_SENSOR};
#endif /* CURRENT_SENSOR_PRESENT == true */

    current_time = OS_GetTickCount();
    DATA_READ_DATA(&staterequestTab, &errorFlagsTab);

    /* Is the BMS still getting CAN messages? */
    if ((current_time - staterequestTab.header.timestamp) <= CAN_TIMING_UPPER_LIMIT_COUNTS) {
        if (((staterequestTab.header.timestamp - staterequestTab.header.previousTimestamp) >=
             CAN_TIMING_LOWER_LIMIT_COUNTS) &&
            ((staterequestTab.header.timestamp - staterequestTab.header.previousTimestamp) <=
             CAN_TIMING_UPPER_LIMIT_COUNTS)) {
            DIAG_Handler(DIAG_ID_CAN_TIMING, DIAG_EVENT_OK, DIAG_SYSTEM, 0u);
        } else {
            DIAG_Handler(DIAG_ID_CAN_TIMING, DIAG_EVENT_NOT_OK, DIAG_SYSTEM, 0u);
        }
    } else {
        DIAG_Handler(DIAG_ID_CAN_TIMING, DIAG_EVENT_NOT_OK, DIAG_SYSTEM, 0u);
    }

#if CURRENT_SENSOR_PRESENT == true
    /* check time stamps of current measurements */
    DATA_READ_DATA(&currentTab);

    for (uint8_t s = 0u; s < BS_NR_OF_STRINGS; s++) {
        /* Current has been measured at least once */
        if (currentTab.timestampCurrent[s] != 0u) {
            /* Check time since last received string current data */
            if ((current_time - currentTab.timestampCurrent[s]) > BS_CURRENT_MEASUREMENT_RESPONSE_TIMEOUT_MS) {
                DIAG_Handler(DIAG_ID_CURRENT_SENSOR_RESPONDING, DIAG_EVENT_NOT_OK, DIAG_STRING, s);
            } else {
                DIAG_Handler(DIAG_ID_CURRENT_SENSOR_RESPONDING, DIAG_EVENT_OK, DIAG_STRING, s);
                if (can_state.currentSensorPresent[s] == false) {
                    CAN_SetCurrentSensorPresent(true, s);
                }
            }
        }

        /* check time stamps of CC measurements */
        /* if timestamp_cc != 0, this means current sensor cc message has been received at least once */
        if (currentTab.timestampCurrentCounting[s] != 0) {
            if ((current_time - currentTab.timestampCurrentCounting[s]) >
                BS_COULOMB_COUNTING_MEASUREMENT_RESPONSE_TIMEOUT_MS) {
                DIAG_Handler(DIAG_ID_CAN_CC_RESPONDING, DIAG_EVENT_NOT_OK, DIAG_STRING, s);
            } else {
                DIAG_Handler(DIAG_ID_CAN_CC_RESPONDING, DIAG_EVENT_OK, DIAG_STRING, s);
                if (can_state.currentSensorCCPresent[s] == false) {
                    CAN_SetCurrentSensorCcPresent(true, s);
                }
            }
        }

        /* check time stamps of EC measurements */
        /* if timestamp_ec != 0, this means current sensor ec message has been received at least once */
        if (currentTab.timestampEnergyCounting[s] != 0) {
            if ((current_time - currentTab.timestampEnergyCounting[s]) >
                BS_ENERGY_COUNTING_MEASUREMENT_RESPONSE_TIMEOUT_MS) {
                DIAG_Handler(DIAG_ID_CAN_EC_RESPONDING, DIAG_EVENT_NOT_OK, DIAG_STRING, s);
            } else {
                DIAG_Handler(DIAG_ID_CAN_EC_RESPONDING, DIAG_EVENT_OK, DIAG_STRING, s);
                if (can_state.currentSensorECPresent[s] == false) {
                    CAN_SetCurrentSensorEcPresent(true, s);
                }
            }
        }
    }
#endif /* CURRENT_SENSOR_PRESENT == true */
}

extern void CAN_ReadRxBuffer(void) {
    CAN_BUFFER_ELEMENT_s can_rxBuffer = {0};
    if (ftsk_allQueuesCreated == true) {
        while (OS_ReceiveFromQueue(ftsk_canRxQueue, (void *)&can_rxBuffer, 0u) == OS_SUCCESS) {
            /* data queue was not empty */
            for (uint16_t i = 0u; i < can_rxLength; i++) {
                if ((can_rxBuffer.canNode == can_rxMessages[i].canNode) &&
                    (can_rxBuffer.id == can_rxMessages[i].message.id)) {
                    if (can_rxMessages[i].callbackFunction != NULL_PTR) {
                        can_rxMessages[i].callbackFunction(can_rxMessages[i].message, can_rxBuffer.data, &can_kShim);
                    }
                }
            }
        }
    }
}

/**
 * @brief   enable/disable the periodic transmit/receive.
 */
extern void CAN_EnablePeriodic(bool command) {
    if (command == true) {
        can_state.periodicEnable = true;
    } else {
        can_state.periodicEnable = false;
    }
}

#if CURRENT_SENSOR_PRESENT == true
static void CAN_SetCurrentSensorPresent(bool command, uint8_t stringNumber) {
    if (command == true) {
        OS_EnterTaskCritical();
        can_state.currentSensorPresent[stringNumber] = true;
        OS_ExitTaskCritical();
    } else {
        OS_EnterTaskCritical();
        can_state.currentSensorPresent[stringNumber] = false;
        OS_ExitTaskCritical();
    }
}

static void CAN_SetCurrentSensorCcPresent(bool command, uint8_t stringNumber) {
    if (command == true) {
        OS_EnterTaskCritical();
        can_state.currentSensorCCPresent[stringNumber] = true;
        OS_ExitTaskCritical();
    } else {
        OS_EnterTaskCritical();
        can_state.currentSensorCCPresent[stringNumber] = false;
        OS_ExitTaskCritical();
    }
}

static void CAN_SetCurrentSensorEcPresent(bool command, uint8_t stringNumber) {
    if (command == true) {
        OS_EnterTaskCritical();
        can_state.currentSensorECPresent[stringNumber] = true;
        OS_ExitTaskCritical();
    } else {
        OS_EnterTaskCritical();
        can_state.currentSensorECPresent[stringNumber] = false;
        OS_ExitTaskCritical();
    }
}
#endif /* CURRENT_SENSOR_PRESENT == true */

extern bool CAN_IsCurrentSensorPresent(uint8_t stringNumber) {
    FAS_ASSERT(stringNumber < BS_NR_OF_STRINGS);
    return can_state.currentSensorPresent[stringNumber];
}

extern bool CAN_IsCurrentSensorCcPresent(uint8_t stringNumber) {
    FAS_ASSERT(stringNumber < BS_NR_OF_STRINGS);
    return can_state.currentSensorCCPresent[stringNumber];
}

extern bool CAN_IsCurrentSensorEcPresent(uint8_t stringNumber) {
    FAS_ASSERT(stringNumber < BS_NR_OF_STRINGS);
    return can_state.currentSensorECPresent[stringNumber];
}

static void CAN_TxInterrupt(canBASE_t *pNode, uint32 messageBox) {
    /* AXIVION Routine Generic-MissingParameterAssert: pNode: unused parameter */
    /* AXIVION Routine Generic-MissingParameterAssert: messageBox: unused parameter */
    (void)pNode;
    (void)messageBox;
}

static void CAN_RxInterrupt(canBASE_t *pNode, uint32 messageBox) {
    FAS_ASSERT(pNode != NULL_PTR);

    CAN_BUFFER_ELEMENT_s can_rxBuffer    = {0u};
    uint8_t messageData[CAN_DEFAULT_DLC] = {0u};
    /**
     *  Read even if queues are not created, otherwise message boxes get full.
     *  Possible return values:
     *   - 0: no new data
     *   - 1: no data lost
     *   - 3: data lost */
    uint32_t retval = canGetData(pNode, messageBox, (uint8 *)&messageData[0]); /* copy to RAM */

    /* Check that CAN RX queue is started before using it and data is valid */
    if ((ftsk_allQueuesCreated == true) && (retval == CAN_HAL_RETVAL_NO_DATA_LOST)) {
        /* id shifted by 18 to use standard frame from IF2ARB register*/
        /* standard frame: bits [28:18] */
        /* extended frame: bits [28:0] */
        uint32_t id = canGetID(pNode, messageBox) >> 18u;

        can_rxBuffer.canNode = pNode;
        can_rxBuffer.id      = id;
        can_rxBuffer.data[0] = messageData[0];
        can_rxBuffer.data[1] = messageData[1];
        can_rxBuffer.data[2] = messageData[2];
        can_rxBuffer.data[3] = messageData[3];
        can_rxBuffer.data[4] = messageData[4];
        can_rxBuffer.data[5] = messageData[5];
        can_rxBuffer.data[6] = messageData[6];
        can_rxBuffer.data[7] = messageData[7];

        if (OS_SendToBackOfQueueFromIsr(ftsk_canRxQueue, (void *)&can_rxBuffer, NULL_PTR) == OS_SUCCESS) {
            /* queue is not full */
            DIAG_Handler(DIAG_ID_CAN_RX_QUEUE_FULL, DIAG_EVENT_OK, DIAG_SYSTEM, 0u);
        } else {
            /* queue is full */
            DIAG_Handler(DIAG_ID_CAN_RX_QUEUE_FULL, DIAG_EVENT_NOT_OK, DIAG_SYSTEM, 0u);
        }
    }
}

/** called in case of CAN interrupt, defined as weak in HAL */
/* AXIVION Next Codeline Style Linker-Multiple_Definition: TI HAL only provides a weak implementation */
void UNIT_TEST_WEAK_IMPL canMessageNotification(canBASE_t *node, uint32 messageBox) {
    /* AXIVION Routine Generic-MissingParameterAssert: node: unchecked in interrupt */
    /* AXIVION Routine Generic-MissingParameterAssert: messageBox: unchecked in interrupt */

    if (messageBox <= CAN_NR_OF_TX_MESSAGE_BOX) {
        CAN_TxInterrupt(node, messageBox);
    } else {
        CAN_RxInterrupt(node, messageBox);
    }
}

/*========== Getter for static Variables (Unit Test) ========================*/
#ifdef UNITY_UNIT_TEST
extern CAN_STATE_s *TEST_CAN_GetCANState(void) {
    return &can_state;
}
#endif

/*========== Externalized Static Function Implementations (Unit Test) =======*/
