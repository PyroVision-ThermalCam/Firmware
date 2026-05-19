# Lepton - Remote-Control

## Table of Contents

- [Lepton - Remote-Control](#lepton---remote-control)
  - [Table of Contents](#table-of-contents)
  - [Available parameters](#available-parameters)
    - [HTTP REST API](#http-rest-api)
      - [POST /api/v1/time](#post-apiv1time)
      - [GET /api/v1/image](#get-apiv1image)
      - [POST /api/v1/settings](#post-apiv1settings)
      - [GET /api/v1/telemetry](#get-apiv1telemetry)
      - [GET /api/v1/info](#get-apiv1info)
      - [GET /api/v1/memory](#get-apiv1memory)
    - [VISA SCPI Commands](#visa-scpi-commands)
      - [System Commands](#system-commands)
      - [Sensor Commands](#sensor-commands)
      - [Display Commands](#display-commands)
      - [Memory Commands](#memory-commands)
    - [WebSocket Commands](#websocket-commands)
      - [Request Format](#request-format)
      - [Response Format](#response-format)
      - [Available Commands](#available-commands)
      - [Example WebSocket Session](#example-websocket-session)
  - [Maintainer](#maintainer)

## Available parameters

| Parameter               | Description                              | Transport        |
|-------------------------|------------------------------------------|------------------|
| Time                    | Set system time and timezone             | HTTP, VISA, WS   |
| Thermal Image           | Retrieve current thermal image           | HTTP             |
| Color Palette           | Active color palette index               | HTTP, VISA, WS   |
| Active Camera           | Switch between thermal and RGB camera    | HTTP             |
| Image Format            | File format of output images             | HTTP, VISA, WS   |
| Telemetry               | Battery, WiFi, temperatures, uptime      | HTTP, WS         |
| Device Info             | Firmware version, palettes, formats      | HTTP             |
| Memory / SD Card        | Storage usage and SD card status         | HTTP, VISA, WS   |
| Lepton Emissivity       | Emissivity of the Lepton camera          | VISA, WS         |
| Lepton Scene Statistics | Scene statistics from the Lepton         | VISA, WS         |
| Lepton ROI              | Region of Interest for the Lepton        | VISA, WS         |
| Lepton Spotmeter        | Spotmeter data from the Lepton           | VISA, WS         |
| Flash Power             | Power of the LED flash                   | VISA, WS         |
| Flash On / Off          | LED flash enable state                   | VISA, WS         |
| Status LED              | Set Status LED color and brightness      | VISA, WS         |
| Format Memory           | Format active memory (flash or SD card)  | VISA, WS         |
| Message Box             | Display a message box on the device      | VISA, WS         |
| Input Lock              | Lock input buttons and joystick          | VISA, WS         |

### HTTP REST API

The HTTP REST API is available at `http://<device-ip>/api/v1`.
Authentication (if configured) requires the `X-API-Key: <key>` header.

| Endpoint             | Method | Python Method       | Description                        |
|----------------------|--------|---------------------|------------------------------------|
| `/api/v1/time`       | POST   | `SetTime()`         | Set system time and timezone       |
| `/api/v1/image`      | GET    | `GetImage()`        | Retrieve current thermal image     |
| `/api/v1/settings`   | POST   | `SetPalette()` / `SetCamera()` / `SetFormat()` | Update device settings |
| `/api/v1/telemetry`  | GET    | `GetTelemetry()`    | Retrieve device telemetry          |
| `/api/v1/info`       | GET    | `GetInfo()`         | Retrieve device information        |
| `/api/v1/memory`     | GET    | `GetMemoryInfo()`   | Retrieve storage usage             |

#### POST /api/v1/time

Sets the system time and optionally the timezone.

**Request:**

```json
{
  "epoch": 1747612800,
  "timezone": "Europe/Berlin"
}
```

**Response:**

```json
{"status": "ok"}
```

#### GET /api/v1/image

Returns the current thermal image as binary data.

**Response:** Binary image data with content type `image/jpeg`, `image/png`, `image/bmp`, or `application/octet-stream`.

#### POST /api/v1/settings

Updates one or more device settings. All keys are optional — only keys present in the request body are applied.

**Supported keys:**

| Key                    | Type   | Description                          |
|------------------------|--------|--------------------------------------|
| `palette`              | number | Active color palette index           |
| `camera.index`         | number | Active camera (0 = thermal, 1 = RGB) |
| `system.image_format`  | number | `ImageEncoder_Format_t` value        |

**Request example:**

```json
{
  "palette": 1,
  "camera": {"index": 0},
  "system": {"image_format": 0}
}
```

**Response:**

```json
{"status": "ok"}
```

#### GET /api/v1/telemetry

Returns current device telemetry data.

**Response:**

```json
{
  "device_uptime_s": 3600,
  "battery_voltage_mv": 3800,
  "battery_percentage": 80,
  "battery_charging": false,
  "wifi_rssi_dbm": -65,
  "temperature_c": 25.3,
  "lepton_fpa_c": 38.5,
  "lepton_aux_c": 35.2,
  "device_temp_c": 28.1
}
```

#### GET /api/v1/info

Returns static device information including firmware version and available palettes/formats.

**Response:**

```json
{
  "firmware_version": "Firmware 1.0.0",
  "build_date": "Jan 1 2026 12:00:00",
  "palettes": [{"index": 0, "name": "Iron"}, ...],
  "image_formats": [{"index": 0, "name": "JPEG"}, ...]
}
```

#### GET /api/v1/memory

Returns storage usage information.

**Response:**

```json
{
  "present": true,
  "sdcard": {"free_mb": 10.5, "total_mb": 16.0, "used_mb": 5.5},
  "coredump": {"free_mb": 0.5, "total_mb": 1.0, "used_mb": 0.5}
}
```

### VISA SCPI Commands

The VISA server (port 5025) supports the following SCPI commands:

#### System Commands

| Command           | Description                      | Example                          |
|-------------------|----------------------------------|----------------------------------|
| `*IDN?`           | Get device identification        | `*IDN?`                          |
| `*RST`            | Reset device                     | `*RST`                           |
| `SYST:TIME?`      | Get current system time          | `SYST:TIME?`                     |
| `SYST:TIME`       | Set system time (ISO 8601)       | `SYST:TIME 2026-01-15T12:30:00`  |
| `SYST:LOCK?`      | Get input lock state             | `SYST:LOCK?`                     |
| `SYST:LOCK`       | Set input lock state             | `SYST:LOCK LOCKED`               |

#### Sensor Commands

| Command                  | Description                      | Example                          |
|--------------------------|----------------------------------|----------------------------------|
| `SENS:TEMP?`             | Get temperature sensor value     | `SENS:TEMP?`                     |
| `SENS:BATT:VOLT?`        | Get battery voltage              | `SENS:BATT:VOLT?`                |
| `SENS:BATT:SOC?`         | Get battery state of charge      | `SENS:BATT:SOC?`                 |
| `SENS:IMG:FORM?`         | Get image format                 | `SENS:IMG:FORM?`                 |
| `SENS:IMG:FORM`          | Set image format                 | `SENS:IMG:FORM JPEG`             |
| `SENS:IMG:LEP:EMIS?`     | Get Lepton emissivity            | `SENS:IMG:LEP:EMIS?`             |
| `SENS:IMG:LEP:EMIS`      | Set Lepton emissivity (0-100)    | `SENS:IMG:LEP:EMIS 98`           |
| `SENS:IMG:LEP:STAT?`     | Get Lepton scene statistics      | `SENS:IMG:LEP:STAT?`             |
| `SENS:IMG:LEP:ROI?`      | Get Lepton ROI                   | `SENS:IMG:LEP:ROI?`              |
| `SENS:IMG:LEP:ROI`       | Set Lepton ROI (JSON)            | `SENS:IMG:LEP:ROI {"x":40,...}`  |
| `SENS:IMG:LEP:SPOT?`     | Get Lepton spotmeter data        | `SENS:IMG:LEP:SPOT?`             |

#### Display Commands

| Command                  | Description                      | Example                          |
|--------------------------|----------------------------------|----------------------------------|
| `DISP:LED:STAT`          | Set status LED                   | `DISP:LED:STAT RED 255`          |
| `DISP:FLASH:POW?`        | Get flash power                  | `DISP:FLASH:POW?`                |
| `DISP:FLASH:POW`         | Set flash power (0-100)          | `DISP:FLASH:POW 50`              |
| `DISP:FLASH:STAT?`       | Get flash state                  | `DISP:FLASH:STAT?`               |
| `DISP:FLASH:STAT`        | Set flash state                  | `DISP:FLASH:STAT ON`             |
| `DISP:MBOX`              | Display message box              | `DISP:MBOX "Hello World"`        |

#### Memory Commands

| Command           | Description                      | Example                          |
|-------------------|----------------------------------|----------------------------------|
| `MEM:SD:STAT?`    | Check SD card availability       | `MEM:SD:STAT?`                   |
| `MEM:FORM`        | Format active memory             | `MEM:FORM`                       |

### WebSocket Commands

The WebSocket endpoint (`ws://device-ip/ws`) supports JSON-formatted commands:

#### Request Format

```json
{
  "cmd": "command_name",
  "data": {
    // command-specific data
  }
}
```

#### Response Format

```json
{
  "cmd": "command_name",
  "status": "ok|error",
  "data": {
    // response data
  },
  "error": "error message (if status=error)"
}
```

#### Available Commands

| Command                    | Description                      | Request Data                     |
|----------------------------|----------------------------------|----------------------------------|
| `get_temperature`          | Get temperature sensor value     | -                                |
| `get_time`                 | Get current system time          | -                                |
| `set_time`                 | Set system time                  | `{"time": "ISO8601"}`            |
| `get_battery`              | Get battery voltage and SOC      | -                                |
| `get_lepton_emissivity`    | Get Lepton emissivity            | -                                |
| `set_lepton_emissivity`    | Set Lepton emissivity            | `{"emissivity": 98}`             |
| `get_lepton_stats`         | Get Lepton scene statistics      | -                                |
| `get_lepton_roi`           | Get Lepton ROI                   | -                                |
| `set_lepton_roi`           | Set Lepton ROI                   | `{"x":40,"y":30,"width":80,...}` |
| `get_lepton_spotmeter`     | Get Lepton spotmeter data        | -                                |
| `get_flash`                | Get flash state and power        | -                                |
| `set_flash`                | Set flash state/power            | `{"enabled":true,"power":50}`    |
| `get_image_format`         | Get current image format         | -                                |
| `set_image_format`         | Set image format                 | `{"format":"JPEG"}`              |
| `set_status_led`           | Set status LED                   | `{"color":"RED","brightness":255}`|
| `get_sd_state`             | Check SD card availability       | -                                |
| `format_memory`            | Format active memory             | -                                |
| `display_message`          | Display message box              | `{"message":"Hello World"}`      |
| `get_lock`                 | Get input lock state             | -                                |
| `set_lock`                 | Set input lock state             | `{"locked":true}`                |

#### Example WebSocket Session

```javascript
// Connect to WebSocket
const ws = new WebSocket('ws://192.168.1.100/ws');

// Get temperature
ws.send(JSON.stringify({
  cmd: "get_temperature",
  data: {}
}));

// Response:
// {"cmd":"temperature","status":"ok","data":{"temperature":25.3}}

// Set Lepton emissivity
ws.send(JSON.stringify({
  cmd: "set_lepton_emissivity",
  data: {"emissivity": 98}
}));

// Response:
// {"cmd":"set_lepton_emissivity","status":"ok"}
```

## Maintainer

**Daniel Kampert**  
📧 [DanielKampert@kampis-elektroecke.de](mailto:DanielKampert@kampis-elektroecke.de)  
🌐 [www.kampis-elektroecke.de](https://www.kampis-elektroecke.de)