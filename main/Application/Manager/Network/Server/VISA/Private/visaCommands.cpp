/*
 * visaCommands.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: VISA commands implementation.
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
#include <esp_timer.h>
#include <esp_heap_caps.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "visaCommands.h"

#include "sdkconfig.h"

/** @brief Error queue */
#define ERROR_QUEUE_SIZE        10

static int _error_queue[ERROR_QUEUE_SIZE];
static size_t _error_count = 0;

/** @brief Operation complete flag */
static bool _operation_complete = true;

static const char *TAG = "VISA-Commands";

/** @brief          Push error to queue
 *  @param Error    Error code
 */
static void VISA_PushError(int Error)
{
    if (_error_count < ERROR_QUEUE_SIZE) {
        _error_queue[_error_count++] = Error;
    }
}

/** @brief          Check if string is a query (ends with ?)
 *  @param Command  Command string
 *  @return         true if query, false otherwise
 */
static bool VISA_IsQuery(const char *Command)
{
    size_t len = strlen(Command);
    return ((len > 0) && (Command[len - 1] == '?'));
}

/** @brief          Parse command into tokens
 *  @param Command  Command string
 *  @param Tokens   Array to store tokens
 *  @param MaxTokens Maximum number of tokens
 *  @return         Number of tokens parsed
 */
static int VISA_ParseCommand(char *Command, char **Tokens, int MaxTokens)
{
    int count = 0;
    char *token = strtok(Command, " \t:");

    while ((token != NULL) && (count < MaxTokens)) {
        Tokens[count++] = token;
        token = strtok(NULL, " \t:");
    }

    return count;
}

/* ===== IEEE 488.2 Common Commands ===== */

/** @brief          *IDN? - Identification query
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_IDN(char *Response, size_t MaxLen)
{
    return 0;
    /*
    return snprintf(Response, MaxLen, "%s,%s,%s,%u.%u.%u\n",
                    CONFIG_NETWORK_VISA_DEVICE_MANUFACTURER,
                    CONFIG_NETWORK_VISA_DEVICE_MODEL,
                    CONFIG_NETWORK_VISA_DEVICE_SERIAL,
                    PYROVISION_VERSION_MAJOR, PYROVISION_VERSION_MINOR, PYROVISION_VERSION_BUILD);
    */
}

/** @brief          *RST - Reset device
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_RST(char *Response, size_t MaxLen)
{
    ESP_LOGI(TAG, "Device reset requested");

    /* TODO: Implement actual reset logic */
    // Reset managers to default state

    _operation_complete = true;
    return 0; /* No response for command */
}

/** @brief          *CLS - Clear status
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_CLS(char *Response, size_t MaxLen)
{
    VISACommands_ClearErrors();
    _operation_complete = true;
    return 0; /* No response */
}

/** @brief          *OPC? - Operation complete query
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_OPC(char *Response, size_t MaxLen)
{
    return snprintf(Response, MaxLen, "%d\n", _operation_complete ? 1 : 0);
}

/** @brief          *TST? - Self-test query
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_TST(char *Response, size_t MaxLen)
{
    /* Perform basic self-test */
    /* 0 = pass, non-zero = fail */
    int result = 0;

    /* TODO: Implement actual self-test */
    // Check camera connection
    // Check display
    // Check memory

    return snprintf(Response, MaxLen, "%d\n", result);
}

/** @brief          SYSTem:ERRor? - Get error from queue
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_SYST_ERR(char *Response, size_t MaxLen)
{
    int error = VISACommands_GetError();

    if (error == SCPI_ERROR_NO_ERROR) {
        return snprintf(Response, MaxLen, "0,\"No error\"\n");
    } else {
        return snprintf(Response, MaxLen, "%d,\"Error %d\"\n", error, error);
    }
}

/** @brief          SYSTem:VERSion? - Get SCPI version
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_SYST_VERS(char *Response, size_t MaxLen)
{
    return snprintf(Response, MaxLen, "1999.0\n"); /* SCPI-99 */
}

/* ===== Device-Specific Commands ===== */

/** @brief          SENSe:TEMPerature? - Get sensor temperature
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_SENS_TEMP(char *Response, size_t MaxLen)
{
    /* TODO: Get actual temperature from Lepton manager */
    float temperature = 25.5f; /* Placeholder */

    return snprintf(Response, MaxLen, "%.2f\n", temperature);
}

/** @brief          SENSe:IMAGE:CAPTure - Capture image
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_SENS_IMG_CAPT(char *Response, size_t MaxLen)
{
    /* TODO: Trigger image capture */
    ESP_LOGI(TAG, "Image capture triggered");

    _operation_complete = false;
    /* Capture happens asynchronously */
    /* Set _operation_complete = true when done */

    return 0; /* No immediate response */
}

/** @brief          SENSe:IMAGE:DATA? - Get captured image data
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative for binary data
 */
static int VISA_CMD_SENS_IMG_DATA(char *Response, size_t MaxLen)
{
    /* TODO: Get actual image data */
    /* This should return binary data in IEEE 488.2 format */
    /* Format: #<n><length><data> where n = digits in length */

    /* Example with dummy data */
    uint8_t *image_data = static_cast<uint8_t *>(heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (image_data == NULL) {
        VISA_PushError(SCPI_ERROR_OUT_OF_MEMORY);
        return SCPI_ERROR_OUT_OF_MEMORY;
    }

    size_t image_size = 1024; /* Placeholder */
    memset(image_data, 0xAA, image_size); /* Dummy data */

    /* Format binary block header */
    char header[32];
    int digits = snprintf(header, sizeof(header), "%zu", image_size);
    int header_len = snprintf(Response, MaxLen, "#%d%zu", digits, image_size);

    /* Copy image data after header */
    if ((header_len + image_size) < MaxLen) {
        memcpy(Response + header_len, image_data, image_size);
        free(image_data);
        return header_len + image_size;
    }

    free(image_data);
    VISA_PushError(SCPI_ERROR_OUT_OF_MEMORY);
    return SCPI_ERROR_OUT_OF_MEMORY;
}

/** @brief          SENSe:IMAGE:FORMat - Set image format
 *  @param Tokens   Command tokens
 *  @param Count    Token count
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_SENS_IMG_FORM(char **Tokens, int Count, char *Response, size_t MaxLen)
{
    if (Count < 4) {
        VISA_PushError(SCPI_ERROR_MISSING_PARAMETER);
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    const char *format = Tokens[3];

    ESP_LOGI(TAG, "Set image format: %s", format);

    /* TODO: Set actual format */
    /* Valid: JPEG, PNG, RAW */

    if ((strcasecmp(format, "JPEG") == 0) ||
        (strcasecmp(format, "PNG") == 0) ||
        (strcasecmp(format, "RAW") == 0)) {
        return 0; /* Success */
    } else {
        VISA_PushError(SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }
}

/** @brief          SENSe:IMAGE:PALette - Set color palette
 *  @param Tokens   Command tokens
 *  @param Count    Token count
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_SENS_IMG_PAL(char **Tokens, int Count, char *Response, size_t MaxLen)
{
    if (Count < 4) {
        VISA_PushError(SCPI_ERROR_MISSING_PARAMETER);
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    const char *palette = Tokens[3];

    ESP_LOGI(TAG, "Set palette: %s", palette);

    /* TODO: Set actual palette */
    /* Valid: IRON, GRAY, RAINBOW */

    if ((strcasecmp(palette, "IRON") == 0) ||
        (strcasecmp(palette, "GRAY") == 0) ||
        (strcasecmp(palette, "RAINBOW") == 0)) {
        return 0; /* Success */
    } else {
        VISA_PushError(SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }
}

/** @brief          DISPlay:LED:STATe - Set LED state
 *  @param Tokens   Command tokens
 *  @param Count    Token count
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_DISP_LED_STAT(char **Tokens, int Count, char *Response, size_t MaxLen)
{
    if (Count < 4) {
        VISA_PushError(SCPI_ERROR_MISSING_PARAMETER);
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    const char *state = Tokens[3];

    ESP_LOGI(TAG, "Set LED state: %s", state);

    /* TODO: Control actual LED */
    /* Valid: ON, OFF, BLINK */

    if ((strcasecmp(state, "ON") == 0) ||
        (strcasecmp(state, "OFF") == 0) ||
        (strcasecmp(state, "BLINK") == 0)) {
        return 0; /* Success */
    } else {
        VISA_PushError(SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }
}

/** @brief          DISPlay:LED:BRIGhtness - Set LED brightness
 *  @param Tokens   Command tokens
 *  @param Count    Token count
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length
 */
static int VISA_CMD_DISP_LED_BRIG(char **Tokens, int Count, char *Response, size_t MaxLen)
{
    if (Count < 4) {
        VISA_PushError(SCPI_ERROR_MISSING_PARAMETER);
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    int brightness = atoi(Tokens[3]);

    if ((brightness < 0) || (brightness > 255)) {
        VISA_PushError(SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    ESP_LOGI(TAG, "Set LED brightness: %d", brightness);

    /* TODO: Set actual LED brightness */

    return 0; /* Success */
}

esp_err_t VISACommands_Init(void)
{
    VISACommands_ClearErrors();
    _operation_complete = true;

    ESP_LOGI(TAG, "VISA command handler initialized");
    return ESP_OK;
}

esp_err_t VISACommands_Deinit(void)
{
    VISACommands_ClearErrors();

    ESP_LOGI(TAG, "VISA command handler deinitialized");
    return ESP_OK;
}

int VISACommands_Execute(const char *Command, char *Response, size_t MaxLen)
{
    if ((Command == NULL) || (Response == NULL)) {
        return SCPI_ERROR_COMMAND_ERROR;
    }

    /* Make a copy for parsing */
    char cmd_copy[256];
    strncpy(cmd_copy, Command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    /* Convert to uppercase for comparison */
    char *tokens[16];
    int token_count = VISA_ParseCommand(cmd_copy, tokens, 16);

    if (token_count == 0) {
        VISA_PushError(SCPI_ERROR_COMMAND_ERROR);
        return SCPI_ERROR_COMMAND_ERROR;
    }

    /* Check for queries */
    bool is_query = VISA_IsQuery(tokens[token_count - 1]);

    /* Remove ? from last token if query */
    if (is_query) {
        size_t len = strlen(tokens[token_count - 1]);
        if (len > 0) {
            tokens[token_count - 1][len - 1] = '\0';
        }
    }

    /* IEEE 488.2 Common Commands */
    if (strcmp(tokens[0], "*IDN") == 0) {
        if (is_query) {
            return VISA_CMD_IDN(Response, MaxLen);
        }
    } else if (strcmp(tokens[0], "*RST") == 0) {
        return VISA_CMD_RST(Response, MaxLen);
    } else if (strcmp(tokens[0], "*CLS") == 0) {
        return VISA_CMD_CLS(Response, MaxLen);
    } else if (strcmp(tokens[0], "*OPC") == 0) {
        if (is_query) {
            return VISA_CMD_OPC(Response, MaxLen);
        }
    } else if (strcmp(tokens[0], "*TST") == 0) {
        if (is_query) {
            return VISA_CMD_TST(Response, MaxLen);
        }
    }
    /* SCPI System Commands */
    else if ((strcasecmp(tokens[0], "SYST") == 0) || (strcasecmp(tokens[0], "SYSTem") == 0)) {
        if ((token_count >= 2) && (strcasecmp(tokens[1], "ERR") == 0 || strcasecmp(tokens[1], "ERRor") == 0)) {
            if (is_query) {
                return VISA_CMD_SYST_ERR(Response, MaxLen);
            }
        } else if ((token_count >= 2) && (strcasecmp(tokens[1], "VERS") == 0 || strcasecmp(tokens[1], "VERSion") == 0)) {
            if (is_query) {
                return VISA_CMD_SYST_VERS(Response, MaxLen);
            }
        }
    }
    /* Device-Specific Commands - SENSe */
    else if ((strcasecmp(tokens[0], "SENS") == 0) || (strcasecmp(tokens[0], "SENSe") == 0)) {
        if ((token_count >= 2) && (strcasecmp(tokens[1], "TEMP") == 0 || strcasecmp(tokens[1], "TEMPerature") == 0)) {
            if (is_query) {
                return VISA_CMD_SENS_TEMP(Response, MaxLen);
            }
        } else if ((token_count >= 2) && (strcasecmp(tokens[1], "IMG") == 0 || strcasecmp(tokens[1], "IMAGE") == 0)) {
            if ((token_count >= 3) && (strcasecmp(tokens[2], "CAPT") == 0 || strcasecmp(tokens[2], "CAPTure") == 0)) {
                return VISA_CMD_SENS_IMG_CAPT(Response, MaxLen);
            } else if ((token_count >= 3) && (strcasecmp(tokens[2], "DATA") == 0)) {
                if (is_query) {
                    return VISA_CMD_SENS_IMG_DATA(Response, MaxLen);
                }
            } else if ((token_count >= 3) && (strcasecmp(tokens[2], "FORM") == 0 || strcasecmp(tokens[2], "FORMat") == 0)) {
                return VISA_CMD_SENS_IMG_FORM(tokens, token_count, Response, MaxLen);
            } else if ((token_count >= 3) && (strcasecmp(tokens[2], "PAL") == 0 || strcasecmp(tokens[2], "PALette") == 0)) {
                return VISA_CMD_SENS_IMG_PAL(tokens, token_count, Response, MaxLen);
            }
        }
    }
    /* Device-Specific Commands - DISPlay */
    else if ((strcasecmp(tokens[0], "DISP") == 0) || (strcasecmp(tokens[0], "DISPlay") == 0)) {
        if ((token_count >= 2) && (strcasecmp(tokens[1], "LED") == 0)) {
            if ((token_count >= 3) && (strcasecmp(tokens[2], "STAT") == 0 || strcasecmp(tokens[2], "STATe") == 0)) {
                return VISA_CMD_DISP_LED_STAT(tokens, token_count, Response, MaxLen);
            } else if ((token_count >= 3) && (strcasecmp(tokens[2], "BRIG") == 0 || strcasecmp(tokens[2], "BRIGhtness") == 0)) {
                return VISA_CMD_DISP_LED_BRIG(tokens, token_count, Response, MaxLen);
            }
        }
    }

    /* Command not found */
    VISA_PushError(SCPI_ERROR_UNDEFINED_HEADER);
    return SCPI_ERROR_UNDEFINED_HEADER;
}

int VISACommands_GetError(void)
{
    if (_error_count > 0) {
        int error = _error_queue[0];

        /* Shift queue */
        for (size_t i = 1; i < _error_count; i++) {
            _error_queue[i - 1] = _error_queue[i];
        }
        _error_count--;

        return error;
    }

    return SCPI_ERROR_NO_ERROR;
}

void VISACommands_ClearErrors(void)
{
    _error_count = 0;
    memset(_error_queue, 0, sizeof(_error_queue));
}
