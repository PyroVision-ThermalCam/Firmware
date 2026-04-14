/*
 * appDiag.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Global diagnostics / error-capture system.
 *             Collects runtime errors from all managers and tasks in a thread-safe ring buffer
 *             together with a microsecond timestamp and a short context string.
 *             The buffer can later be read back for on-screen display, VISA queries,
 *             or logging to SD card.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Errors and commissions should be reported to DanielKampert@kampis-elektroecke.de
 */

#ifndef APP_DIAG_H_
#define APP_DIAG_H_

#include <esp_err.h>

#include <stdint.h>
#include <stdbool.h>

/** @brief Maximum number of diagnostic entries kept in the ring buffer.
 *         Oldest entry is silently overwritten once the buffer is full.
 */
#define APP_DIAG_MAX_ENTRIES        32

/** @brief Maximum length of the context string (including NUL terminator).
 */
#define APP_DIAG_CONTEXT_LEN        48

/** @defgroup APP_DIAG_SOURCES Diagnostic Source Identifiers
 *  @brief Identifies which manager or task produced a diagnostic entry.
 *  @{
 */

/** @brief Identifies the subsystem that recorded a diagnostic entry. */
typedef enum {
    APP_DIAG_SOURCE_DEVICES = 0,    /**< Devices Manager. */
    APP_DIAG_SOURCE_MEMORY,         /**< Memory Manager. */
    APP_DIAG_SOURCE_NETWORK,        /**< Network Manager. */
    APP_DIAG_SOURCE_SETTINGS,       /**< Settings Manager. */
    APP_DIAG_SOURCE_TIME,           /**< Time Manager. */
    APP_DIAG_SOURCE_USB,            /**< USB Manager. */
    APP_DIAG_SOURCE_APPLICATION,    /**< Application / task layer. */
    APP_DIAG_SOURCE_TASK_LEPTON,    /**< Lepton camera task. */
    APP_DIAG_SOURCE_TASK_NETWORK,   /**< Network task. */
    APP_DIAG_SOURCE_TASK_DEVICES,   /**< Devices task. */
    APP_DIAG_SOURCE_TASK_GUI,       /**< GUI task. */
    APP_DIAG_SOURCE_TASK_CAMERA,    /**< Camera task. */
    APP_DIAG_SOURCE_COUNT,          /**< Number of source identifiers — keep last. */
} AppDiag_Source_t;

/** @} */

/** @brief Single diagnostic entry stored in the ring buffer.
 */
typedef struct {
    int64_t TimestampUs;                        /**< Microseconds since boot (esp_timer_get_time()). */
    AppDiag_Source_t Source;                    /**< Manager or subsystem that recorded the entry. */
    esp_err_t ErrorCode;                        /**< esp_err_t value (may be a manager-specific code). */
    char Context[APP_DIAG_CONTEXT_LEN];         /**< Short NUL-terminated context string (caller-supplied). */
} AppDiag_Entry_t;

/** @brief          Initialise the diagnostics module.
 *                  Creates the internal mutex and prepares the ring buffer.
 *                  Must be called once from the main task before any manager is started.
 *                  Entries recorded before AppDiag_Init() via AppDiag_RecordError() are
 *                  accepted but written without mutex protection (safe during single-threaded boot).
 *  @note           Call this as early as possible — ideally as the very first operation in app_main().
 *  @warning        Not thread-safe during the call itself. Call once only.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if already initialised
 *                  ESP_ERR_NO_MEM if mutex creation fails
 */
esp_err_t AppDiag_Init(void);

/** @brief              Record an error in the diagnostic ring buffer.
 *                      If the buffer is full the oldest entry is overwritten.
 *                      Thread-safe after AppDiag_Init() has been called; safe (but unprotected)
 *                      before init.
 *  @note               This function never blocks and never returns an error value, so it is safe
 *                      to call from any context including error-handling paths.
 *  @param Source       Subsystem that detected the error.
 *  @param ErrorCode    esp_err_t error value to record (may be a manager-specific code).
 *  @param p_Context    Optional NUL-terminated context string (e.g. function name).
 *                      Pass NULL if no context is available.
 */
void AppDiag_RecordError(AppDiag_Source_t Source, esp_err_t ErrorCode, const char *p_Context);

/** @brief          Return the total number of entries currently stored in the ring buffer.
 *  @note           Thread-safe.
 *  @return         Number of entries (0 .. APP_DIAG_MAX_ENTRIES).
 */
uint32_t AppDiag_GetCount(void);

/** @brief          Return true if at least one error entry has been recorded.
 *  @note           Thread-safe.
 *  @return         true  if one or more entries are present
 *                  false if the buffer is empty
 */
bool AppDiag_HasErrors(void);

/** @brief          Read a single entry from the ring buffer by index.
 *                  Index 0 is the oldest stored entry; index (Count-1) is the newest.
 *  @note           Thread-safe.
 *                  Indices shift when new entries arrive (ring buffer semantics).
 *  @param Index    Zero-based index into [0 .. AppDiag_GetCount()-1].
 *  @param p_Entry  Pointer to caller-allocated AppDiag_Entry_t to fill.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Entry is NULL
 *                  ESP_ERR_NOT_FOUND if Index >= AppDiag_GetCount()
 */
esp_err_t AppDiag_GetEntry(uint32_t Index, AppDiag_Entry_t *p_Entry);

/** @brief          Clear all entries in the ring buffer.
 *  @note           Thread-safe.
 */
void AppDiag_Clear(void);

/** @brief          Convenience macro: record an error and use the calling function's name
 *                  as the context string automatically.
 *  @param source   AppDiag_Source_t value identifying the calling manager or task.
 *  @param code     esp_err_t error code to record.
 */
#define APP_DIAG_RECORD(source, code)   AppDiag_RecordError((source), (code), __func__)

#endif /* APP_DIAG_H_ */
