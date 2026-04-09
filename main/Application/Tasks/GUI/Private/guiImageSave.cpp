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
#include "Application/application.h"
#include "Application/Manager/Memory/memoryManager.h"

extern GUI_Task_State_t _GUI_Task_State;

static const char *TAG = "GUI-ImgSave";

void Task_ImageSave(void *p_Param)
{
    App_Lepton_FrameReady_t Frame;
    char FilePath[128];

    ESP_LOGD(TAG, "Image save task started");

    while (true) {
        uint32_t Caps;

        if (xQueueReceive(_GUI_Task_State.ImageSaveQueue, &Frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Check if filesystem is locked (USB active) */
        if (MemoryManager_IsFilesystemLocked()) {
            ESP_LOGW(TAG, "Cannot save image - USB mode active!");

            esp_event_post(GUI_EVENTS, GUI_EVENT_THERMAL_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        /* Validate frame data */
        if ((Frame.Buffer == NULL) || (Frame.Width == 0) || (Frame.Height == 0)) {
            ESP_LOGE(TAG, "Invalid frame data!");

            esp_event_post(GUI_EVENTS, GUI_EVENT_THERMAL_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        const char *p_StoragePath = MemoryManager_GetStoragePath();
        static uint32_t ImageCounter = 0;
        snprintf(FilePath, sizeof(FilePath), "%s/IMG_%03u.PNG", p_StoragePath, (unsigned int)(ImageCounter % 1000));
        ImageCounter++;

        ESP_LOGD(TAG, "Saving PNG: %s (%dx%d)", FilePath, Frame.Width, Frame.Height);

        /* Open file for writing PNG */
        FILE *PNGFile = fopen(FilePath, "wb");
        if (PNGFile == NULL) {
            ESP_LOGE(TAG, "Failed to open file for writing: %s", FilePath);

            esp_event_post(GUI_EVENTS, GUI_EVENT_THERMAL_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        /* Initialize PNG structures */
        png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (png_ptr == NULL) {
            ESP_LOGE(TAG, "Failed to create PNG write struct!");

            fclose(PNGFile);
            esp_event_post(GUI_EVENTS, GUI_EVENT_THERMAL_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        png_infop info_ptr = png_create_info_struct(png_ptr);
        if (info_ptr == NULL) {
            ESP_LOGE(TAG, "Failed to create PNG info struct!");

            png_destroy_write_struct(&png_ptr, NULL);
            fclose(PNGFile);
            esp_event_post(GUI_EVENTS, GUI_EVENT_THERMAL_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

#ifdef CONFIG_SPIRAM
        Caps = MALLOC_CAP_SPIRAM ;
#else
        Caps = 0;
#endif

        /* Allocate line buffer before setjmp (to avoid crossing initialization) */
        uint8_t *LineBuffer = static_cast<uint8_t *>(heap_caps_malloc(Frame.Width * 3, Caps));
        if (LineBuffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate line buffer!");

            png_destroy_write_struct(&png_ptr, &info_ptr);
            fclose(PNGFile);
            esp_event_post(GUI_EVENTS, GUI_EVENT_THERMAL_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

            continue;
        }

        /* Set error handling */
        if (setjmp(png_jmpbuf(png_ptr))) {
            ESP_LOGE(TAG, "PNG encoding error!");

            heap_caps_free(LineBuffer);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            fclose(PNGFile);
            esp_event_post(GUI_EVENTS, GUI_EVENT_THERMAL_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));

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

        /* Write image data row by row */
        for (uint32_t y = 0; y < Frame.Height; y++) {
            for (uint32_t x = 0; x < Frame.Width; x++) {
                uint32_t idx = (y * Frame.Width + x) * 2;  /* RGB565 = 2 bytes per pixel */
                uint16_t rgb565 = Frame.Buffer[idx] | (Frame.Buffer[idx + 1] << 8);

                /* Convert RGB565 to RGB888 */
                uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
                uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
                uint8_t b = (rgb565 & 0x1F) << 3;

                /* PNG uses RGB format */
                LineBuffer[x * 3 + 0] = r;
                LineBuffer[x * 3 + 1] = g;
                LineBuffer[x * 3 + 2] = b;
            }

            png_write_row(png_ptr, LineBuffer);
        }

        /* Finish writing PNG */
        png_write_end(png_ptr, NULL);

        heap_caps_free(LineBuffer);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(PNGFile);

        /* Get file size for logging */
        struct stat FileStat;
        if (stat(FilePath, &FileStat) == 0) {
            ESP_LOGD(TAG, "PNG image saved: %s (%u bytes)", FilePath, static_cast<uint32_t>(FileStat.st_size));
        } else {
            ESP_LOGD(TAG, "PNG image saved: %s", FilePath);
        }

        esp_event_post(GUI_EVENTS, GUI_EVENT_THERMAL_IMAGE_SAVED, NULL, 0, pdMS_TO_TICKS(100));
    }
}
