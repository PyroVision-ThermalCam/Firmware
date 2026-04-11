/*
 * memoryTypes.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Common type definitions for the Memory Manager component.
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

#ifndef MEMORY_TYPES_H
#define MEMORY_TYPES_H

#include <esp_err.h>
#include <esp_event.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MEMORY_ERR_BASE                     0x2000

/** @defgroup MEMORY_ERRORS Memory Manager Error Codes
 *  @brief Error codes returned by MemoryManager functions (base: @c MEMORY_ERR_BASE = 0x2000).
 *  @{
 */

/** @brief MemoryManager_Init() has not been called yet. */
#define MEMORY_ERR_NOT_INITIALIZED              (MEMORY_ERR_BASE + 0x01)

/** @brief Internal flash (LittleFS) mount failed. */
#define MEMORY_ERR_FLASH_MOUNT                  (MEMORY_ERR_BASE + 0x02)

/** @brief SD card mount failed (card present but unreadable or unsupported filesystem). */
#define MEMORY_ERR_SD_MOUNT                     (MEMORY_ERR_BASE + 0x03)

/** @brief SD card not present in the slot. */
#define MEMORY_ERR_SD_NOT_PRESENT               (MEMORY_ERR_BASE + 0x04)

/** @brief Storage is full — no free space available on the active storage medium. */
#define MEMORY_ERR_STORAGE_FULL                 (MEMORY_ERR_BASE + 0x05)

/** @brief Filesystem unmount failed during deinitialisation. */
#define MEMORY_ERR_UNMOUNT                      (MEMORY_ERR_BASE + 0x06)

/** @brief Wear levelling handle initialisation failed (internal flash only). */
#define MEMORY_ERR_WEAR_LEVELLING               (MEMORY_ERR_BASE + 0x07)

/** @brief NULL pointer or invalid parameter passed to a MemoryManager function. */
#define MEMORY_ERR_INVALID_ARG                  (MEMORY_ERR_BASE + 0x08)

/** @brief Precondition not met (e.g. manager not initialized, wrong storage location). */
#define MEMORY_ERR_INVALID_STATE                (MEMORY_ERR_BASE + 0x09)

/** @brief Requested partition, resource, or item was not found. */
#define MEMORY_ERR_NOT_FOUND                    (MEMORY_ERR_BASE + 0x0A)

/** @} */

/** @brief Memory Manager events base.
 */
ESP_EVENT_DECLARE_BASE(MEMORY_EVENTS);

/** @brief Memory Manager event identifiers.
 */
enum {
    MEMORY_EVENT_SD_CARD_MOUNTED,       /**< SD card was successfully mounted and is ready for use. */
    MEMORY_EVENT_FLASH_MOUNTED,         /**< Internal flash storage was mounted and is ready for use. */
    MEMORY_EVENT_SD_CARD_UNMOUNTED,     /**< SD card was unmounted and storage switched back to internal flash. */
    MEMORY_EVENT_SD_CARD_MOUNT_ERROR,   /**< SD card was detected but mounting failed. */
    MEMORY_EVENT_FLASH_MOUNT_ERROR      /**< Internal flash mount failed. */
};

#endif /* MEMORY_TYPES_H */
