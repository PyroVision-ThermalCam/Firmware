# Lepton - Remote-Control

## Table of Contents

- [Lepton - Remote-Control](#lepton---remote-control)
  - [Table of Contents](#table-of-contents)
  - [Available parameters](#available-parameters)
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

| Parameter               | Description                              | Methods  |
|-------------------------|------------------------------------------|----------|
| Temperature Sensor Value| Temperature from the TMP117 sensor       | GET      |
| Time                    | Current system time                      | GET, SET |
| Battery Voltage         | Voltage of the battery                   | GET      |
| State-Of-Charge         | Battery state of charge                  | GET      |
| OV5640 Image            | Image from OV5640 camera                 | GET      |
| Lepton Image            | Image from Lepton camera                 | GET      |
| Lepton Emissivity       | Emissivity of Lepton camera              | GET, SET |
| Lepton Scene Statistics | Scene statistics from Lepton             | GET      |
| Lepton ROI              | Region of Interest for Lepton            | GET, SET |
| Lepton Spotmeter        | Spotmeter data from Lepton               | GET      |
| Flash Power             | Power of the flash                       | GET, SET |
| Flash On / Off          | Flash state                              | GET, SET |
| Image Format            | File format of the output images (PNG, Raw, JPEG)       | GET, SET |
| Status LED              | Set the Status LED (Red, Green, Blue)                      | SET      |
| State SD card           | Checks if an SD card is available       | GET      |
| Format                  | Format the active memory (internal flash or SD card) | SET |
| Messagebox Display      | Display a message box with a given text | SET      |
| Lock                    | Lock the input buttons and the joystick | GET, SET      |

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