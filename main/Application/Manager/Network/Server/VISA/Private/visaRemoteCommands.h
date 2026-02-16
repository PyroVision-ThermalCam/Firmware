/*
 * visaRemoteCommands.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: VISA commands for remote control interface.
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

#ifndef VISA_REMOTE_COMMANDS_H_
#define VISA_REMOTE_COMMANDS_H_

#include <stddef.h>

/** @brief          Handle SENS:TEMP? - Get temperature sensor value.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetTemperature(char *p_Response, size_t MaxLen);

/** @brief          Handle SYST:TIME? - Get system time.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetTime(char *p_Response, size_t MaxLen);

/** @brief          Handle SYST:TIME - Set system time.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetTime(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:BATT:VOLT? - Get battery voltage.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetBatteryVoltage(char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:BATT:SOC? - Get state of charge.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetStateOfCharge(char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:IMG:LEP:EMIS? - Get Lepton emissivity.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetLeptonEmissivity(char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:IMG:LEP:EMIS - Set Lepton emissivity.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetLeptonEmissivity(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:IMG:LEP:STAT? - Get Lepton scene statistics.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetLeptonStats(char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:IMG:LEP:ROI? - Get Lepton ROI.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetLeptonROI(char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:IMG:LEP:ROI - Set Lepton ROI.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetLeptonROI(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:IMG:LEP:SPOT? - Get Lepton spotmeter.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetLeptonSpotmeter(char *p_Response, size_t MaxLen);

/** @brief          Handle DISP:FLASH:POW? - Get flash power.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetFlashPower(char *p_Response, size_t MaxLen);

/** @brief          Handle DISP:FLASH:POW - Set flash power.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetFlashPower(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle DISP:FLASH:STAT? - Get flash state.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetFlashState(char *p_Response, size_t MaxLen);

/** @brief          Handle DISP:FLASH:STAT - Set flash state.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetFlashState(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:IMG:FORM? - Get image format.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetImageFormat(char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:IMG:FORM - Set image format.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetImageFormat(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle DISP:LED:STAT - Set status LED.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetStatusLED(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle MEM:SD:STAT? - Get SD card state.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetSDCardState(char *p_Response, size_t MaxLen);

/** @brief          Handle MEM:FORM - Format memory.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_FormatMemory(char *p_Response, size_t MaxLen);

/** @brief          Handle DISP:MBOX - Display message box.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_DisplayMessageBox(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle SYST:LOCK? - Get lock state.
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_GetLockState(char *p_Response, size_t MaxLen);

/** @brief          Handle SYST:LOCK - Set lock state.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetLockState(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle SENS:IMG:PAL - Set image color palette.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetImagePalette(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle DISP:LED:BRIG - Set LED brightness.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetLEDBrightness(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);
/** @brief          Handle SENS:IMG:PAL - Set image color palette.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetImagePalette(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);

/** @brief          Handle DISP:LED:BRIG - Set LED brightness.
 *  @param pp_Tokens Command tokens
 *  @param Count    Token count
 *  @param p_Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or negative error code
 */
int VISA_Cmd_SetLEDBrightness(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen);
#endif /* VISA_REMOTE_COMMANDS_H_ */
