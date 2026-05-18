/*
 * guiImageSave.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: GUI task image save implementation.
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

#include <esp_log.h>
#include <esp_event.h>

#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <png.h>

#include "guiImageSave.h"
#include "Application/app_types.h"
#include "Application/Manager/Memory/memoryManager.h"

extern GUI_Task_State_t _GUITaskState;

static const char *TAG = "GUI-ImgSave";

void Task_ImageSave(void *p_Param)
{
    App_Lepton_Frame_t Frame;
    char FilePath[128];

    ESP_LOGD(TAG, "Image save task started");

    while (true) {
        if (xQueueReceive(_GUITaskState.ImageSaveQueue, &Frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Check if filesystem is locked (USB active) */
        if (MemoryManager_IsFilesystemLocked()) {
            ESP_LOGW(TAG, "Cannot save image - USB mode active!");

            esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        /* Validate frame data */
        if ((Frame.Buffer == NULL) || (Frame.Width == 0) || (Frame.Height == 0)) {
            ESP_LOGE(TAG, "Invalid frame data!");

            esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        const char *p_StoragePath = MemoryManager_GetStoragePath();
        static uint32_t ImageCounter = 0;
        snprintf(FilePath, sizeof(FilePath), "%s/IMG_%03u.PNG", p_StoragePath, static_cast<unsigned int>(ImageCounter % 1000));
        ImageCounter++;

        ESP_LOGD(TAG, "Saving PNG: %s (%dx%d)", FilePath, Frame.Width, Frame.Height);

        /* Open file for writing PNG */
        xSemaphoreTake(_GUITaskState.SpiMutex, portMAX_DELAY);

        FILE *PNGFile = fopen(FilePath, "wb");
        if (PNGFile == NULL) {
            ESP_LOGE(TAG, "Failed to open file for writing: %s", FilePath);

            xSemaphoreGive(_GUITaskState.SpiMutex);
            esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        /* Initialize PNG structures */
        png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (png_ptr == NULL) {
            ESP_LOGE(TAG, "Failed to create PNG write struct!");

            fclose(PNGFile);
            xSemaphoreGive(_GUITaskState.SpiMutex);
            esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        png_infop info_ptr = png_create_info_struct(png_ptr);
        if (info_ptr == NULL) {
            ESP_LOGE(TAG, "Failed to create PNG info struct!");

            png_destroy_write_struct(&png_ptr, NULL);
            fclose(PNGFile);
            xSemaphoreGive(_GUITaskState.SpiMutex);
            esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        /* Allocate line buffer before setjmp (to avoid crossing initialization) */
        uint8_t *LineBuffer = static_cast<uint8_t *>(heap_caps_malloc(Frame.Width * 3, MALLOC_CAP_SPIRAM));
        if (LineBuffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate line buffer!");

            png_destroy_write_struct(&png_ptr, &info_ptr);
            fclose(PNGFile);
            xSemaphoreGive(_GUITaskState.SpiMutex);
            esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        /* Set error handling */
        if (setjmp(png_jmpbuf(png_ptr))) {
            ESP_LOGE(TAG, "PNG encoding error!");

            heap_caps_free(LineBuffer);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            fclose(PNGFile);
            xSemaphoreGive(_GUITaskState.SpiMutex);
            esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        /* Set up PNG output */
        png_init_io(png_ptr, PNGFile);

        /* Set PNG image parameters */
        png_set_IHDR(png_ptr, info_ptr, Frame.Width, Frame.Height,
                     8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

        /* Write PNG header */
        png_write_info(png_ptr, info_ptr);

        /* Write image data row by row.
         * The canvas buffer has a 180° pre-rotation applied so that LVGL can display it
         * upright via lv_image_set_rotation(1800). Reading in reverse order (bottom-to-top,
         * right-to-left) undoes that rotation so the saved PNG is correctly oriented. */
        for (uint32_t y = 0; y < Frame.Height; y++) {
            for (uint32_t x = 0; x < Frame.Width; x++) {
                /* Undo 180° canvas pre-rotation: read from the opposite corner */
                uint32_t SrcY = Frame.Height - 1 - y;
                uint32_t SrcX = Frame.Width - 1 - x;
                uint32_t Idx = (SrcY * Frame.Width + SrcX) * 2;  /* RGB565 = 2 bytes per pixel */
                uint16_t Rgb565 = Frame.Buffer[Idx] | (Frame.Buffer[Idx + 1] << 8);

                /* Convert RGB565 to RGB888 */
                uint8_t R = ((Rgb565 >> 11) & 0x1F) << 3;
                uint8_t G = ((Rgb565 >> 5) & 0x3F) << 2;
                uint8_t B = (Rgb565 & 0x1F) << 3;

                /* PNG uses RGB format */
                LineBuffer[x * 3 + 0] = R;
                LineBuffer[x * 3 + 1] = G;
                LineBuffer[x * 3 + 2] = B;
            }

            png_write_row(png_ptr, LineBuffer);
        }

        /* Finish writing PNG */
        png_write_end(png_ptr, NULL);

        heap_caps_free(LineBuffer);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(PNGFile);
        xSemaphoreGive(_GUITaskState.SpiMutex);

        /* Get file size for logging */
        struct stat FileStat;
        if (stat(FilePath, &FileStat) == 0) {
            ESP_LOGD(TAG, "PNG image saved: %s (%u bytes)", FilePath, static_cast<uint32_t>(FileStat.st_size));
        } else {
            ESP_LOGD(TAG, "PNG image saved: %s", FilePath);
        }

        esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVED, NULL, 0, pdMS_TO_TICKS(100));
    }
}

esp_err_t GUI_SaveImage(void)
{
    /* Check if filesystem is locked (USB active) */
    if (MemoryManager_IsFilesystemLocked()) {
        ESP_LOGW(TAG, "Cannot save image - USB mode active!");

        return ESP_ERR_INVALID_STATE;
    }

    /* Set flag to trigger save on next frame update */
    _GUITaskState.SaveNextFrameRequested = true;
    ESP_LOGD(TAG, "Image save requested - will capture next frame");

    return ESP_OK;
}