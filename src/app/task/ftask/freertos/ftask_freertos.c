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
 * @file    ftask_freertos.c
 * @author  foxBMS Team
 * @date    2019-08-27 (date of creation)
 * @updated 2022-10-27 (date of last update)
 * @version v1.4.1
 * @ingroup TASK
 * @prefix  FTSK
 *
 * @brief   OS specific, i.e., FreeRTOS specfific, creation of the tasks
 * @details TODO
 */

/*========== Includes =======================================================*/
#include "can_cfg.h"

#include "FreeRTOS.h"
#include "task.h"

#include "database.h"
#include "ftask.h"

/*========== Macros and Definitions =========================================*/
/** helper macro to translate the stack sizes from bytes into words as FreeRTOS requires words and not bytes */
#define FTSK_BYTES_TO_WORDS(VARIABALE_IN_BYTES) ((VARIABALE_IN_BYTES) / GEN_BYTES_PER_WORD)
/** Stack size of engine task in words */
#define FTSK_TASK_ENGINE_STACK_SIZE_IN_WORDS FTSK_BYTES_TO_WORDS(FTSK_TASK_ENGINE_STACK_SIZE_IN_BYTES)
/** @brief Stack size of cyclic 1 ms task in words */
#define FTSK_TASK_CYCLIC_1MS_STACK_SIZE_IN_WORDS FTSK_BYTES_TO_WORDS(FTSK_TASK_CYCLIC_1MS_STACK_SIZE_IN_BYTES)
/** @brief Stack size of cyclic 10 ms task in words */
#define FTSK_TASK_CYCLIC_10MS_STACK_SIZE_IN_WORDS FTSK_BYTES_TO_WORDS(FTSK_TASK_CYCLIC_10MS_STACK_SIZE_IN_BYTES)
/** @brief Stack size of cyclic 100 ms task in words */
#define FTSK_TASK_CYCLIC_100MS_STACK_SIZE_IN_WORDS FTSK_BYTES_TO_WORDS(FTSK_TASK_CYCLIC_100MS_STACK_SIZE_IN_BYTES)
/** @brief Stack size of cyclic 100 ms task for algorithms in words */
#define FTSK_TASK_CYCLIC_ALGORITHM_100MS_STACK_SIZE_IN_WORDS \
    FTSK_BYTES_TO_WORDS(FTSK_TASK_CYCLIC_ALGORITHM_100MS_STACK_SIZE_IN_BYTES)
/** @brief Stack size of continuously running task for AFEs */
#define FTSK_TASK_AFE_STACK_SIZE_IN_WORDS FTSK_BYTES_TO_WORDS(FTSK_TASK_AFE_STACK_SIZE_IN_BYTES)

/** size of storage area for the database queue */
#define FTSK_DATABASE_QUEUE_STORAGE_AREA (FTSK_DATABASE_QUEUE_LENGTH * FTSK_DATABASE_QUEUE_ITEM_SIZE_IN_BYTES)

/** size of storage area for the IMD queue*/
#define FTSK_IMD_QUEUE_STORAGE_AREA (FTSK_IMD_QUEUE_LENGTH * FTSK_IMD_QUEUE_ITEM_SIZE_IN_BYTES)

/** size of storage area for the CAN Rx queue*/
#define FTSK_CAN_RX_QUEUE_STORAGE_AREA (FTSK_CAN_RX_QUEUE_LENGTH * FTSK_CAN_RX_QUEUE_ITEM_SIZE_IN_BYTES)

/*========== Static Constant and Variable Definitions =======================*/

/*========== Extern Constant and Variable Definitions =======================*/
/** @brief Definition of task handle for the AFE task */
TaskHandle_t ftsk_taskHandleAfe;

volatile bool ftsk_allQueuesCreated = false;

QueueHandle_t ftsk_databaseQueue = NULL_PTR;

QueueHandle_t ftsk_imdCanDataQueue = NULL_PTR;

/* INCLUDE MARKER FOR THE DOCUMENTATION; DO NOT MOVE can-documentation-rx-queue-handle-start-include */
QueueHandle_t ftsk_canRxQueue = NULL_PTR;
/* INCLUDE MARKER FOR THE DOCUMENTATION; DO NOT MOVE can-documentation-rx-queue-handle-stop-include */

/*========== Static Function Prototypes =====================================*/

/*========== Static Function Implementations ================================*/

/*========== Extern Function Implementations ================================*/

extern void FTSK_CreateQueues(void) {
    /* structure and array for static database queue */
    static uint8_t ftsk_databaseQueueStorageArea[FTSK_DATABASE_QUEUE_STORAGE_AREA] = {0};
    static StaticQueue_t ftsk_databaseQueueStructure                               = {0};

    /* Create a queue capable of containing a pointer of type DATA_QUEUE_MESSAGE_s
       Data of Messages are passed by pointer as they contain a lot of data. */
    ftsk_databaseQueue = xQueueCreateStatic(
        FTSK_DATABASE_QUEUE_LENGTH,
        FTSK_DATABASE_QUEUE_ITEM_SIZE_IN_BYTES,
        ftsk_databaseQueueStorageArea,
        &ftsk_databaseQueueStructure);
    FAS_ASSERT(ftsk_databaseQueue != NULL);
    vQueueAddToRegistry(ftsk_databaseQueue, "Database Queue");

    /* structure and array for static IMD queue */
    static uint8_t ftsk_imdQueueStorageArea[FTSK_IMD_QUEUE_STORAGE_AREA] = {0};
    static StaticQueue_t ftsk_imdQueueStructure                          = {0};

    ftsk_imdCanDataQueue = xQueueCreateStatic(
        FTSK_IMD_QUEUE_LENGTH, FTSK_IMD_QUEUE_ITEM_SIZE_IN_BYTES, ftsk_imdQueueStorageArea, &ftsk_imdQueueStructure);
    vQueueAddToRegistry(ftsk_imdCanDataQueue, "IMD CAN Data Queue");
    FAS_ASSERT(ftsk_imdCanDataQueue != NULL);

    /* INCLUDE MARKER FOR THE DOCUMENTATION; DO NOT MOVE can-documentation-rx-queue-vars-start-include */
    /* structure and array for static CAN RX queue */
    static uint8_t ftsk_canRxQueueStorageArea[FTSK_CAN_RX_QUEUE_STORAGE_AREA] = {0};
    static StaticQueue_t ftsk_canRxQueueStructure                             = {0};
    /* INCLUDE MARKER FOR THE DOCUMENTATION; DO NOT MOVE can-documentation-rx-queue-vars-stop-include */

    ftsk_canRxQueue = xQueueCreateStatic(
        FTSK_CAN_RX_QUEUE_LENGTH,
        FTSK_CAN_RX_QUEUE_ITEM_SIZE_IN_BYTES,
        ftsk_canRxQueueStorageArea,
        &ftsk_canRxQueueStructure);
    vQueueAddToRegistry(ftsk_canRxQueue, "CAN Receive Queue");
    FAS_ASSERT(ftsk_canRxQueue != NULL);

    OS_EnterTaskCritical();
    ftsk_allQueuesCreated = true;
    OS_ExitTaskCritical();
}

extern void FTSK_CreateTasks(void) {
    /* Engine Task */
    static StaticTask_t ftsk_taskEngine                                       = {0};
    static StackType_t ftsk_stackEngine[FTSK_TASK_ENGINE_STACK_SIZE_IN_WORDS] = {0};

    const TaskHandle_t ftsk_taskHandleEngine = xTaskCreateStatic(
        (TaskFunction_t)FTSK_CreateTaskEngine,
        (const portCHAR *)"TaskEngine",
        FTSK_BYTES_TO_WORDS(ftsk_taskDefinitionEngine.stackSize_B),
        (void *)ftsk_taskDefinitionEngine.pvParameters,
        (UBaseType_t)ftsk_taskDefinitionEngine.priority,
        ftsk_stackEngine,
        &ftsk_taskEngine);
    FAS_ASSERT(ftsk_taskHandleEngine != NULL); /* Trap if initialization failed */

    /* Cyclic Task 1ms */
    static StaticTask_t ftsk_taskCyclic1ms                                           = {0};
    static StackType_t ftsk_stackCyclic1ms[FTSK_TASK_CYCLIC_1MS_STACK_SIZE_IN_WORDS] = {0};

    const TaskHandle_t ftsk_taskHandleCyclic1ms = xTaskCreateStatic(
        (TaskFunction_t)FTSK_CreateTaskCyclic1ms,
        (const portCHAR *)"TaskCyclic1ms",
        FTSK_BYTES_TO_WORDS(ftsk_taskDefinitionCyclic1ms.stackSize_B),
        (void *)ftsk_taskDefinitionCyclic1ms.pvParameters,
        (UBaseType_t)ftsk_taskDefinitionCyclic1ms.priority,
        ftsk_stackCyclic1ms,
        &ftsk_taskCyclic1ms);
    FAS_ASSERT(ftsk_taskHandleCyclic1ms != NULL); /* Trap if initialization failed */

    /* Cyclic Task 10ms */
    static StaticTask_t ftsk_taskCyclic10ms                                            = {0};
    static StackType_t ftsk_stackCyclic10ms[FTSK_TASK_CYCLIC_10MS_STACK_SIZE_IN_WORDS] = {0};

    const TaskHandle_t ftsk_taskHandleCyclic10ms = xTaskCreateStatic(
        (TaskFunction_t)FTSK_CreateTaskCyclic10ms,
        (const portCHAR *)"TaskCyclic10ms",
        FTSK_BYTES_TO_WORDS(ftsk_taskDefinitionCyclic10ms.stackSize_B),
        (void *)ftsk_taskDefinitionCyclic10ms.pvParameters,
        (UBaseType_t)ftsk_taskDefinitionCyclic10ms.priority,
        ftsk_stackCyclic10ms,
        &ftsk_taskCyclic10ms);
    FAS_ASSERT(ftsk_taskHandleCyclic10ms != NULL); /* Trap if initialization failed */

    /* Cyclic Task 100ms */
    static StaticTask_t ftsk_taskCyclic100ms                                             = {0};
    static StackType_t ftsk_stackCyclic100ms[FTSK_TASK_CYCLIC_100MS_STACK_SIZE_IN_WORDS] = {0};

    const TaskHandle_t ftsk_taskHandleCyclic100ms = xTaskCreateStatic(
        (TaskFunction_t)FTSK_CreateTaskCyclic100ms,
        (const portCHAR *)"TaskCyclic100ms",
        FTSK_BYTES_TO_WORDS(ftsk_taskDefinitionCyclic100ms.stackSize_B),
        (void *)ftsk_taskDefinitionCyclic100ms.pvParameters,
        (UBaseType_t)ftsk_taskDefinitionCyclic100ms.priority,
        ftsk_stackCyclic100ms,
        &ftsk_taskCyclic100ms);
    FAS_ASSERT(ftsk_taskHandleCyclic100ms != NULL); /* Trap if initialization failed */

    /* Cyclic Task 100ms for algorithms */
    static StaticTask_t ftsk_taskCyclicAlgorithm100ms                                                       = {0};
    static StackType_t ftsk_stackCyclicAlgorithm100ms[FTSK_TASK_CYCLIC_ALGORITHM_100MS_STACK_SIZE_IN_WORDS] = {0};

    const TaskHandle_t ftsk_taskHandleCyclicAlgorithm100ms = xTaskCreateStatic(
        (TaskFunction_t)FTSK_CreateTaskCyclicAlgorithm100ms,
        (const portCHAR *)"TaskCyclicAlgorithm100ms",
        FTSK_BYTES_TO_WORDS(ftsk_taskDefinitionCyclicAlgorithm100ms.stackSize_B),
        (void *)ftsk_taskDefinitionCyclicAlgorithm100ms.pvParameters,
        (UBaseType_t)ftsk_taskDefinitionCyclicAlgorithm100ms.priority,
        ftsk_stackCyclicAlgorithm100ms,
        &ftsk_taskCyclicAlgorithm100ms);
    FAS_ASSERT(ftsk_taskHandleCyclicAlgorithm100ms != NULL); /* Trap if initialization failed */

    /* Continuously running Task for AFE */
    static StaticTask_t ftsk_taskAfe                                        = {0};
    static StackType_t ftsk_stackSizeAfe[FTSK_TASK_AFE_STACK_SIZE_IN_WORDS] = {0};

    ftsk_taskHandleAfe = xTaskCreateStatic(
        (TaskFunction_t)FTSK_CreateTaskAfe,
        (const portCHAR *)"TaskAfe",
        FTSK_BYTES_TO_WORDS(ftsk_taskDefinitionAfe.stackSize_B),
        (void *)ftsk_taskDefinitionAfe.pvParameters,
        (UBaseType_t)ftsk_taskDefinitionAfe.priority,
        ftsk_stackSizeAfe,
        &ftsk_taskAfe);
    FAS_ASSERT(ftsk_taskHandleAfe != NULL); /* Trap if initialization failed */
}

/*========== Externalized Static Function Implementations (Unit Test) =======*/
