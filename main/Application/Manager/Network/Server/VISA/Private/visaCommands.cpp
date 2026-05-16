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

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

#include "visaCommands.h"
#include "visaRemoteCommands.h"
#include "Settings/settingsManager.h"

#include "sdkconfig.h"

static int VISA_ErrorQueue[CONFIG_NETWORK_VISA_ERROR_QUEUE_LLENGTH];
static size_t VISA_ErrorCount = 0;

/** @brief Operation complete flag */
static bool VISA_OperationComplete = true;

static const char *TAG = "VISA-Commands";

/** @brief          Push error to queue
 *  @param Error    Error code
 */
static void VISA_PushError(int Error)
{
    if (VISA_ErrorCount < CONFIG_NETWORK_VISA_ERROR_QUEUE_LLENGTH) {
        VISA_ErrorQueue[VISA_ErrorCount++] = Error;
    }
}

/** @brief          Case-insensitive string comparison
 *  @param s1       First string
 *  @param s2       Second string
 *  @return         true if equal (case-insensitive)
 */
static bool string_iequals(const std::string& s1, const std::string& s2)
{
    return std::equal(s1.begin(), s1.end(), s2.begin(), s2.end(),
    [](char a, char b) {
        return std::tolower(a) == std::tolower(b);
    });
}

/** @brief          Check if string is a query (ends with ?)
 *  @param Command  Command string
 *  @return         true if query, false otherwise
 */
static bool VISA_IsQuery(const std::string& Command)
{
    return (Command.empty() == false) && (Command.back() == '?');
}

/** @brief          Parse command into tokens
 *  @param Command  Command string
 *  @return         Vector of token strings
 */
static std::vector<std::string> VISA_ParseCommand(const std::string& Command)
{
    std::vector<std::string> TokenList;
    std::string Token;

    for (char c : Command) {
        if ((c == ' ') || (c == '\t') || (c == ':')) {
            if ((Token.empty() == false)) {
                TokenList.push_back(Token);
                Token.clear();
            }
        } else {
            Token += c;
        }
    }

    if (Token.empty() == false) {
        TokenList.push_back(Token);
    }

    return TokenList;
}

/* ===== IEEE 488.2 Common Commands ===== */

/** @brief              *IDN? - Identification query
 *  @param p_Response   Response buffer
 *  @param MaxLen       Maximum response length
 *  @return             Response length
 */
static int VISA_CMD_IDN(char *p_Response, size_t MaxLen)
{
    size_t Length;
    Settings_Info_t Info;
    std::ostringstream oss;
    std::string Result;

    SettingsManager_GetInfo(&Info);

    /* Build response using C++ string for safety */
    oss << (Info.Manufacturer[0] ? Info.Manufacturer : "PyroVision") << ","
        << (Info.Name[0] ? Info.Name : "ThermalCam") << ","
        << (Info.Serial[0] ? Info.Serial : "00000001") << ","
        << (Info.FirmwareVersion[0] ? Info.FirmwareVersion : "1.0.0") << "\n";

    Result = oss.str();

    /* Copy to output buffer */
    Length = std::min(Result.length(), MaxLen - 1);
    memcpy(p_Response, Result.c_str(), Length);
    p_Response[Length] = '\0';

    ESP_LOGI(TAG, "*IDN? response: %s", Result.c_str());

    return static_cast<int>(Length);
}

/** @brief              *RST - Reset device
 *  @param p_Response   Response buffer
 *  @param MaxLen       Maximum response length
 *  @return             Response length
 */
static int VISA_CMD_RST(char *p_Response, size_t MaxLen)
{
    ESP_LOGI(TAG, "Device reset requested");

    /* TODO: Implement actual reset logic */
    // Reset managers to default state

    VISA_OperationComplete = true;

    /* No response for command */
    return 0;
}

/** @brief              *CLS - Clear status
 *  @param p_Response   Response buffer
 *  @param MaxLen       Maximum response length
 *  @return             Response length
 */
static int VISA_CMD_CLS(char *p_Response, size_t MaxLen)
{
    VISACommands_ClearErrors();
    VISA_OperationComplete = true;

    /* No response for command */
    return 0;
}

/** @brief              *OPC? - Operation complete query
 *  @param p_Response   Response buffer
 *  @param MaxLen       Maximum response length
 *  @return             Response length
 */
static int VISA_CMD_OPC(char *p_Response, size_t MaxLen)
{
    size_t Length;
    std::string Result;

    Result = std::to_string(VISA_OperationComplete ? 1 : 0) + "\n";
    Length = std::min(Result.length(), MaxLen - 1);
    memcpy(p_Response, Result.c_str(), Length);
    p_Response[Length] = '\0';

    return static_cast<int>(Length);
}

/** @brief              *TST? - Self-test query
 *  @param p_Response   Response buffer
 *  @param MaxLen       Maximum response length
 *  @return             Response length
 */
static int VISA_CMD_TST(char *p_Response, size_t MaxLen)
{
    /* Perform basic self-test */
    /* 0 = pass, non-zero = fail */
    int Result = 0;
    size_t Length;
    std::string Response;

    /* TODO: Implement actual self-test */
    // Check camera connection
    // Check display
    // Check memory

    Response = std::to_string(Result) + "\n";
    Length = std::min(Response.length(), MaxLen - 1);
    memcpy(p_Response, Response.c_str(), Length);
    p_Response[Length] = '\0';

    return static_cast<int>(Length);
}

/** @brief              SYSTem:ERRor? - Get error from queue
 *  @param p_Response   Response buffer
 *  @param MaxLen       Maximum response length
 *  @return             Response length
 */
static int VISA_CMD_SYST_ERR(char *p_Response, size_t MaxLen)
{
    size_t Length;
    std::string Result;
    std::ostringstream oss;
    int Error;

    Error = VISACommands_GetError();
    if (Error == SCPI_ERROR_NO_ERROR) {
        oss << "0,\"No error\"\n";
    } else {
        oss << Error << ",\"Error " << Error << "\"\n";
    }

    Result = oss.str();
    Length = std::min(Result.length(), MaxLen - 1);
    memcpy(p_Response, Result.c_str(), Length);
    p_Response[Length] = '\0';

    return static_cast<int>(Length);
}

/** @brief              SYSTem:VERSion? - Get SCPI version
 *  @param p_Response   Response buffer
 *  @param MaxLen       Maximum response length
 *  @return             Response length
 */
static int VISA_CMD_SYST_VERS(char *p_Response, size_t MaxLen)
{
    std::string Result = "1999.0\n"; /* SCPI-99 */
    size_t Length;

    Length = std::min(Result.length(), MaxLen - 1);
    memcpy(p_Response, Result.c_str(), Length);
    p_Response[Length] = '\0';

    return static_cast<int>(Length);
}

/* ===== Device-Specific Commands ===== */

/** @brief              SENSe:IMAGE:CAPTure - Capture image
 *  @param p_Response   Response buffer
 *  @param MaxLen       Maximum response length
 *  @return             Response length
 */
static int VISA_CMD_SENS_IMG_CAPT(char *p_Response, size_t MaxLen)
{
    /* TODO: Trigger image capture */
    ESP_LOGI(TAG, "Image capture triggered");

    VISA_OperationComplete = false;
    /* Capture happens asynchronously */
    /* Set _operation_complete = true when done */

    return 0; /* No immediate response */
}

/** @brief              SENSe:IMAGE:DATA? - Get captured image data
 *  @param p_Response   Response buffer
 *  @param MaxLen       Maximum response length
 *  @return             Response length or negative for binary data
 */
static int VISA_CMD_SENS_IMG_DATA(char *p_Response, size_t MaxLen)
{
    char Header[32];
    int HeaderSize;
    int Digits;
    size_t Size = 1024;
    uint8_t *Data;

    /* TODO: Get actual image data */
    /* This should return binary data in IEEE 488.2 format */
    /* Format: #<n><length><data> where n = digits in length */

    /* Example with dummy data */
    Data = static_cast<uint8_t *>(heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (Data == NULL) {
        VISA_PushError(SCPI_ERROR_OUT_OF_MEMORY);

        return SCPI_ERROR_OUT_OF_MEMORY;
    }

    memset(Data, 0xAA, Size); /* Dummy data */

    /* Format binary block header */
    Digits = snprintf(Header, sizeof(Header), "%zu", Size);
    HeaderSize = snprintf(p_Response, MaxLen, "#%d%zu", Digits, Size);

    /* Copy image data after header */
    if ((HeaderSize + Size) < MaxLen) {
        memcpy(p_Response + HeaderSize, Data, Size);
        free(Data);

        return HeaderSize + Size;
    }

    free(Data);
    VISA_PushError(SCPI_ERROR_OUT_OF_MEMORY);

    return SCPI_ERROR_OUT_OF_MEMORY;
}

esp_err_t VISACommands_Init(void)
{
    VISACommands_ClearErrors();
    VISA_OperationComplete = true;

    ESP_LOGD(TAG, "VISA command handler initialized");

    return ESP_OK;
}

esp_err_t VISACommands_Deinit(void)
{
    VISACommands_ClearErrors();

    ESP_LOGD(TAG, "VISA command handler deinitialized");

    return ESP_OK;
}

int VISACommands_Execute(const char *Command, char *Response, size_t MaxLen)
{
    std::vector<std::string> Tokens;
    bool IsQuery;

    if ((Command == NULL) || (Response == NULL)) {
        return SCPI_ERROR_COMMAND_ERROR;
    }

    /* Convert to C++ string and parse */
    Tokens = VISA_ParseCommand(std::string(Command));
    if (Tokens.empty()) {
        VISA_PushError(SCPI_ERROR_COMMAND_ERROR);

        return SCPI_ERROR_COMMAND_ERROR;
    }

    /* Check for queries */
    IsQuery = VISA_IsQuery(Tokens.back());

    /* Remove ? from last token if query */
    if (IsQuery && (Tokens.back().empty() == false)) {
        Tokens.back().pop_back();
    }

    /* IEEE 488.2 Common Commands */
    if (Tokens[0] == "*IDN") {
        if (IsQuery) {
            return VISA_CMD_IDN(Response, MaxLen);
        }
    } else if (Tokens[0] == "*RST") {
        return VISA_CMD_RST(Response, MaxLen);
    } else if (Tokens[0] == "*CLS") {
        return VISA_CMD_CLS(Response, MaxLen);
    } else if (Tokens[0] == "*OPC") {
        if (IsQuery) {
            return VISA_CMD_OPC(Response, MaxLen);
        }
    } else if (Tokens[0] == "*TST") {
        if (IsQuery) {
            return VISA_CMD_TST(Response, MaxLen);
        }
    }
    /* SCPI System Commands */
    else if (string_iequals(Tokens[0], "SYST") || string_iequals(Tokens[0], "SYSTem")) {
        if ((Tokens.size() >= 2) && (string_iequals(Tokens[1], "ERR") || string_iequals(Tokens[1], "ERRor"))) {
            if (IsQuery) {
                return VISA_CMD_SYST_ERR(Response, MaxLen);
            }
        } else if ((Tokens.size() >= 2) && (string_iequals(Tokens[1], "VERS") || string_iequals(Tokens[1], "VERSion"))) {
            if (IsQuery) {
                return VISA_CMD_SYST_VERS(Response, MaxLen);
            }
        } else if ((Tokens.size() >= 2) && string_iequals(Tokens[1], "TIME")) {
            if (IsQuery) {
                return VISA_Cmd_GetTime(Response, MaxLen);
            } else {
                std::vector<char *> TokenList;

                for (auto& t : Tokens) {
                    TokenList.push_back(const_cast<char*>(t.c_str()));
                }

                return VISA_Cmd_SetTime(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
            }
        } else if ((Tokens.size() >= 2) && string_iequals(Tokens[1], "LOCK")) {
            if (IsQuery) {
                return VISA_Cmd_GetLockState(Response, MaxLen);
            } else {
                std::vector<char *> TokenList;

                for (auto& t : Tokens) {
                    TokenList.push_back(const_cast<char*>(t.c_str()));
                }

                return VISA_Cmd_SetLockState(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
            }
        }
    }
    /* Device-Specific Commands - SENSe */
    else if (string_iequals(Tokens[0], "SENS") || string_iequals(Tokens[0], "SENSe")) {
        if ((Tokens.size() >= 2) && (string_iequals(Tokens[1], "TEMP") || string_iequals(Tokens[1], "TEMPerature"))) {
            if (IsQuery) {
                return VISA_Cmd_GetTemperature(Response, MaxLen);
            }
        } else if ((Tokens.size() >= 2) && (string_iequals(Tokens[1], "BATT") || string_iequals(Tokens[1], "BATTery"))) {
            if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "VOLT") || string_iequals(Tokens[2], "VOLTage"))) {
                if (IsQuery) {
                    return VISA_Cmd_GetBatteryVoltage(Response, MaxLen);
                }
            }
        } else if ((Tokens.size() >= 2) && (string_iequals(Tokens[1], "IMG") || string_iequals(Tokens[1], "IMAGE"))) {
            if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "CAPT") || string_iequals(Tokens[2], "CAPTure"))) {
                return VISA_CMD_SENS_IMG_CAPT(Response, MaxLen);
            } else if ((Tokens.size() >= 3) && string_iequals(Tokens[2], "DATA")) {
                if (IsQuery) {
                    return VISA_CMD_SENS_IMG_DATA(Response, MaxLen);
                }
            } else if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "FORM") || string_iequals(Tokens[2], "FORMat"))) {
                if (IsQuery) {
                    return VISA_Cmd_GetImageFormat(Response, MaxLen);
                } else {
                    std::vector<char *> TokenList;

                    for (auto& t : Tokens) {
                        TokenList.push_back(const_cast<char*>(t.c_str()));
                    }

                    return VISA_Cmd_SetImageFormat(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
                }
            } else if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "PAL") || string_iequals(Tokens[2], "PALette"))) {
                std::vector<char *> TokenList;

                for (auto& t : Tokens) {
                    TokenList.push_back(const_cast<char*>(t.c_str()));
                }

                return VISA_Cmd_SetImagePalette(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
            } else if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "LEP") || string_iequals(Tokens[2], "LEPton"))) {
                if ((Tokens.size() >= 4) && (string_iequals(Tokens[3], "EMIS") || string_iequals(Tokens[3], "EMISsivity"))) {
                    if (IsQuery) {
                        return VISA_Cmd_GetLeptonEmissivity(Response, MaxLen);
                    } else {
                        std::vector<char *> TokenList;

                        for (auto& t : Tokens) {
                            TokenList.push_back(const_cast<char*>(t.c_str()));
                        }

                        return VISA_Cmd_SetLeptonEmissivity(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
                    }
                } else if ((Tokens.size() >= 4) && (string_iequals(Tokens[3], "STAT") || string_iequals(Tokens[3], "STATistics"))) {
                    if (IsQuery) {
                        return VISA_Cmd_GetLeptonStats(Response, MaxLen);
                    }
                } else if ((Tokens.size() >= 4) && string_iequals(Tokens[3], "ROI")) {
                    if (IsQuery) {
                        return VISA_Cmd_GetLeptonROI(Response, MaxLen);
                    } else {
                        std::vector<char *> TokenList;

                        for (auto& t : Tokens) {
                            TokenList.push_back(const_cast<char*>(t.c_str()));
                        }

                        return VISA_Cmd_SetLeptonROI(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
                    }
                } else if ((Tokens.size() >= 4) && (string_iequals(Tokens[3], "SPOT") || string_iequals(Tokens[3], "SPOTmeter"))) {
                    if (IsQuery) {

                        return VISA_Cmd_GetLeptonSpotmeter(Response, MaxLen);
                    }
                }
            }
        }
    }
    /* Device-Specific Commands - DISPlay */
    else if (string_iequals(Tokens[0], "DISP") || string_iequals(Tokens[0], "DISPlay")) {
        if ((Tokens.size() >= 2) && string_iequals(Tokens[1], "LED")) {
            if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "STAT") || string_iequals(Tokens[2], "STATe"))) {
                std::vector<char *> TokenList;

                for (auto& t : Tokens) {
                    TokenList.push_back(const_cast<char*>(t.c_str()));
                }

                return VISA_Cmd_SetStatusLED(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
            } else if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "BRIG") || string_iequals(Tokens[2], "BRIGhtness"))) {
                std::vector<char *> TokenList;

                for (auto& t : Tokens) {
                    TokenList.push_back(const_cast<char*>(t.c_str()));
                }

                return VISA_Cmd_SetLEDBrightness(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
            }
        } else if ((Tokens.size() >= 2) && string_iequals(Tokens[1], "FLASH")) {
            if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "POW") || string_iequals(Tokens[2], "POWer"))) {
                if (IsQuery) {
                    return VISA_Cmd_GetFlashPower(Response, MaxLen);
                } else {
                    std::vector<char *> TokenList;

                    for (auto& t : Tokens) {
                        TokenList.push_back(const_cast<char*>(t.c_str()));
                    }

                    return VISA_Cmd_SetFlashPower(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
                }
            } else if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "STAT") || string_iequals(Tokens[2], "STATe"))) {
                if (IsQuery) {
                    return VISA_Cmd_GetFlashState(Response, MaxLen);
                } else {
                    std::vector<char *> TokenList;

                    for (auto& t : Tokens) {
                        TokenList.push_back(const_cast<char*>(t.c_str()));
                    }

                    return VISA_Cmd_SetFlashState(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
                }
            }
        } else if ((Tokens.size() >= 2) && (string_iequals(Tokens[1], "MBOX") || string_iequals(Tokens[1], "MessageBOX"))) {
            std::vector<char *> TokenList;

            for (auto& t : Tokens) {
                TokenList.push_back(const_cast<char*>(t.c_str()));
            }

            return VISA_Cmd_DisplayMessageBox(TokenList.data(), static_cast<int>(Tokens.size()), Response, MaxLen);
        }
    }
    /* Device-Specific Commands - MEMory */
    else if (string_iequals(Tokens[0], "MEM") || string_iequals(Tokens[0], "MEMory")) {
        if ((Tokens.size() >= 2) && string_iequals(Tokens[1], "SD")) {
            if ((Tokens.size() >= 3) && (string_iequals(Tokens[2], "STAT") || string_iequals(Tokens[2], "STATe"))) {
                if (IsQuery) {
                    return VISA_Cmd_GetSDCardState(Response, MaxLen);
                }
            }
        } else if ((Tokens.size() >= 2) && (string_iequals(Tokens[1], "FORM") || string_iequals(Tokens[1], "FORMat"))) {
            return VISA_Cmd_FormatMemory(Response, MaxLen);
        }
    }

    /* Command not found */
    VISA_PushError(SCPI_ERROR_UNDEFINED_HEADER);

    return SCPI_ERROR_UNDEFINED_HEADER;
}

int VISACommands_GetError(void)
{
    if (VISA_ErrorCount > 0) {
        int Error;

        Error = VISA_ErrorQueue[0];

        /* Shift queue */
        for (size_t i = 1; i < VISA_ErrorCount; i++) {
            VISA_ErrorQueue[i - 1] = VISA_ErrorQueue[i];
        }

        VISA_ErrorCount--;

        return Error;
    }

    return SCPI_ERROR_NO_ERROR;
}

void VISACommands_ClearErrors(void)
{
    VISA_ErrorCount = 0;
    memset(VISA_ErrorQueue, 0, sizeof(VISA_ErrorQueue));
}
