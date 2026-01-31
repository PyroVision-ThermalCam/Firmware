# GitHub Copilot Instructions for PyroVision Firmware

## Project Overview

PyroVision is an ESP32-S3 based thermal imaging camera firmware using the ESP-IDF framework. The project manages a Lepton thermal camera with WiFi connectivity, web interface, VISA server, and comprehensive settings management.

**Key Technologies:**
- Platform: ESP32-S3 (ESP-IDF framework)
- Build System: PlatformIO
- RTOS: FreeRTOS
- GUI: LVGL
- Storage: NVS, LittleFS, SD Card
- Networking: WiFi (STA/AP), HTTP Server, WebSockets, VISA/SCPI

---

## Code Style and Formatting

### General Formatting Rules

The project uses **Artistic Style (AStyle)** with a K&R-based configuration (`scripts/astyle.cfg`):

- **Style**: K&R (Kernighan & Ritchie)
- **Indentation**: 4 spaces (no tabs)
- **Line Length**: Maximum 120 characters
- **Braces**: K&R style (opening brace on same line for functions, control structures)
- **Operators**: Space padding around operators: `a = bar((b - c) * a, d--);`
- **Headers**: Space between header and bracket: `if (condition) {`
- **Pointers/References**: Stick to name: `char *pThing`, `char &thing`
- **Conditionals**: Always use braces, even for single-line blocks
- **Switch**: Indent case statements

**Example:**
```cpp
esp_err_t MyFunction(uint8_t *p_Buffer, size_t Size)
{
    if (p_Buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (size_t i = 0; i < Size; i++) {
        p_Buffer[i] = ProcessByte(p_Buffer[i]);
    }
    
    return ESP_OK;
}
```

### Naming Conventions

#### Functions
- **Public API**: `ModuleName_FunctionName()` using PascalCase
  - Examples: `SettingsManager_Init()`, `NetworkManager_Connect()`, `TimeManager_SetTimezone()`
- **Private/Static**: `snake_case` with descriptive names
  - Examples: `on_WiFi_Event()`, `run_astyle()`

#### Variables
- **Local variables**: `PascalCase` for important variables, `lowercase` for simple counters
  - Examples: `RetryCount`, `EventGroup`, `Error`, `i`, `x`
- **Pointers**: Prefix with `p` (e.g., `p_Settings`, `p_Buffer`, `p_Data`)
- **Global/Static module state**: Prefix with underscore: `_State`, `_Network_Manager_State`, `_App_Context`

#### Constants and Macros
- **All UPPERCASE** with underscores: `WIFI_CONNECTED_BIT`, `NVS_NAMESPACE`, `SETTINGS_VERSION`
- Module-specific macros should include module name: `SETTINGS_NVS_NAMESPACE`, `VISA_MAX_CLIENTS`

#### Types
- **Structs/Enums**: `ModuleName_Description_t` with `_t` suffix
  - Examples: `App_Settings_t`, `Network_State_t`, `App_Settings_WiFi_t`
- **Enums**: Use descriptive prefix for values
  - Example: `SETTINGS_EVENT_LOADED`, `NETWORK_EVENT_WIFI_CONNECTED`

#### File Names
- Header files: `moduleName.h`
- Implementation files: `moduleName.cpp`
- Types/definitions: `moduleTypes.h`
- Private implementations: `Private/internalModule.cpp`

### Code Organization

#### Directory Structure
```
main/
├── main.cpp                   # Application entry point
├── Application/
│   ├── application.h          # Application-wide types and events
│   ├── Manager/               # All manager modules
│   │   ├── managers.h         # Manager includes
│   │   ├── Settings/          # Settings management
│   │   ├── Network/           # Network management
│   │   ├── Devices/           # Device drivers
│   │   ├── Time/              # Time management
│   │   └── SD/                # SD card management
│   └── Tasks/                 # FreeRTOS tasks
│       ├── tasks.h            # Task declarations
│       ├── GUI/               # GUI task
│       ├── Lepton/            # Camera task
│       └── Network/           # Network task
```

#### Private Implementations
Use `Private/` subdirectories for internal module implementations that should not be exposed:
```
Manager/Settings/
├── settingsManager.h         # Public API
├── settingsManager.cpp       # Implementation
├── settingsTypes.h          # Public types
└── Private/
    ├── settingsLoader.h     # Internal interface
    ├── settingsJSONLoader.cpp
    └── settingsDefaultLoader.cpp
```

---

## License and Copyright

### File Headers

**Every source file** (`.cpp`, `.h`, `.py`) must include the following header:

```cpp
/*
 * filename.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Brief description of the file's purpose.
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
```

For Python files:
```python
"""
filename.py

Copyright (C) Daniel Kampert, 2026
Website: www.kampis-elektroecke.de
File info: Brief description of the file's purpose.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
"""
```

**License**: GNU General Public License v3.0  
**Copyright Holder**: Daniel Kampert  
**Contact**: DanielKampert@kampis-elektroecke.de  
**Website**: www.kampis-elektroecke.de

---

## Documentation Standards

### Function Documentation Requirements

**CRITICAL**: Every function declaration in header files MUST include complete Doxygen documentation.

#### Doxygen-Style Documentation for ALL Functions

Use Doxygen comments for **ALL** public API functions with complete documentation:

```cpp
/** @brief          Initialize the settings manager and load configuration from NVS.
 *  
 *  This function initializes the settings subsystem, opens the NVS namespace,
 *  and attempts to load stored settings. If no settings exist, default values
 *  are loaded from JSON or hardcoded defaults.
 *  
 *  @return         ESP_OK on success
 *  @return         ESP_ERR_NVS_NOT_FOUND if settings namespace doesn't exist
 *  @return         ESP_ERR_NO_MEM if memory allocation fails
 *  @return         ESP_ERR_INVALID_STATE if already initialized
 *  
 *  @note           Must be called after NVS flash initialization.
 *  @note           This function must be called before any other SettingsManager API calls.
 *  @warning        Not thread-safe during initialization. Call once from main task.
 */
esp_err_t SettingsManager_Init(void);
```

**Mandatory documentation elements:**
- `@brief` - Short one-line description
- Detailed description paragraph explaining what the function does
- `@param` - Description for EACH parameter (include direction: in/out/inout if relevant)
- `@return` - Document ALL possible return values (one `@return` per value)
- `@note` - Important usage notes (at least one recommended)
- `@warning` - Critical warnings about misuse or side effects (if applicable)

#### Incomplete Documentation is NOT Acceptable

❌ **Insufficient:**
```cpp
/** @brief Brief description.
 *  @param p_Param Description
 *  @return ESP_OK on success
 */
```

✅ **Complete:**
```cpp
/** @brief          Set WiFi credentials and update configuration.
 *  
 *  Updates the WiFi SSID and password in RAM and posts a SETTINGS_EVENT_WIFI_CHANGED
 *  event. Changes are not persisted until SettingsManager_Save() is called.
 *  
 *  @param p_SSID   Pointer to null-terminated SSID string (max 32 chars)
 *  @param p_Pass   Pointer to null-terminated password string (max 64 chars)
 *  
 *  @return         ESP_OK on success
 *  @return         ESP_ERR_INVALID_ARG if p_SSID or p_Pass is NULL
 *  @return         ESP_ERR_INVALID_ARG if strings exceed maximum length
 *  @return         ESP_ERR_INVALID_STATE if SettingsManager not initialized
 *  
 *  @note           Call SettingsManager_Save() to persist changes to NVS.
 *  @note           This function is thread-safe.
 *  @warning        Password is stored in plain text in NVS.
 */
esp_err_t SettingsManager_SetWiFi(const char *p_SSID, const char *p_Pass);
```

### Code Comments

#### Inline Comments
- Use `//` for single-line comments
- Use `/* */` for block comments
- Add explanatory comments for complex logic
- Comment "why", not "what" (the code shows what)

**Example:**
```cpp
/* Reset config_loaded flag to allow reloading default config */
Error = nvs_set_u8(_State.NVS_Handle, "config_loaded", false);
```

### Structure Documentation

Document struct members inline:

```cpp
typedef struct {
    uint16_t Port;              /**< HTTP server port. */
    uint16_t WSPingIntervalSec; /**< WebSocket ping interval in seconds. */
    uint8_t MaxClients;         /**< Maximum number of simultaneous clients. */
} App_Settings_HTTP_Server_t;
```

### Enum Documentation

```cpp
/** @brief Settings event identifiers.
 */
enum {
    SETTINGS_EVENT_LOADED,      /**< Settings loaded from NVS. */
    SETTINGS_EVENT_SAVED,       /**< Settings saved to NVS. */
    SETTINGS_EVENT_WIFI_CHANGED,/**< WiFi settings changed.
                                     Data contains App_Settings_WiFi_t. */
};
```

### External Documentation

For complex modules, create documentation in `docs/` directory:
- Use **AsciiDoc** (`.adoc`) for technical documentation
- Use **Markdown** (`.md`) for README-style documentation
- Include architecture diagrams, API references, usage examples, and troubleshooting

---

## ESP-IDF Specific Conventions

### Error Handling

- **Always check return values** from ESP-IDF functions
- Use `ESP_ERROR_CHECK()` for critical initialization that should abort on failure
- Use manual error handling for recoverable errors:

```cpp
esp_err_t Error = nvs_open(NAMESPACE, NVS_READWRITE, &Handle);
if (Error != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %d", Error);
    return Error;
}
```

### Logging

Use ESP-IDF logging macros with appropriate levels:
- `ESP_LOGE(TAG, ...)` - Errors
- `ESP_LOGW(TAG, ...)` - Warnings
- `ESP_LOGI(TAG, ...)` - Information
- `ESP_LOGD(TAG, ...)` - Debug

Define TAG at the top of each file:
```cpp
static const char *TAG = "module_name";
```

### Event System

- Use ESP Event system for module communication
- Define event bases: `ESP_EVENT_DEFINE_BASE(MODULE_EVENTS);`
- Declare in headers: `ESP_EVENT_DECLARE_BASE(MODULE_EVENTS);`
- Use descriptive event IDs in enums
- Always include documentation about event data payload

### FreeRTOS

- Task names should be descriptive: `"DevicesTask"`, `"NetworkTask"`
- Use appropriate priorities (defined in task headers)
- Always check if queue/semaphore creation succeeded
- Use `portMAX_DELAY` for blocking operations unless timeout is critical
- Prefer queues for inter-task communication

---

## Module Design Patterns

### Manager Pattern

Managers are stateful modules that provide a cohesive API for a subsystem:

```cpp
// Public API pattern
esp_err_t ModuleName_Init(void);
esp_err_t ModuleName_Deinit(void);
esp_err_t ModuleName_GetConfig(Module_Config_t *p_Config);
esp_err_t ModuleName_UpdateConfig(Module_Config_t *p_Config);
esp_err_t ModuleName_Save(void);

// Internal state (static in .cpp)
static Module_State_t _State;
```

### Thread-Safe Access Pattern

For shared resources:

```cpp
typedef struct {
    SemaphoreHandle_t Mutex;
    // ... data fields
} Module_State_t;

esp_err_t Module_GetData(Data_t *p_Data)
{
    if (p_Data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Data, &_State.Data, sizeof(Data_t));
    xSemaphoreGive(_State.Mutex);
    
    return ESP_OK;
}
```

### Event-Driven Updates

When updating module state:
1. Validate parameters
2. Acquire mutex
3. Update state
4. Release mutex
5. Post event to notify listeners

```cpp
esp_err_t Module_Update(Config_t *p_Config)
{
    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(&_State.Config, p_Config, sizeof(Config_t));
    xSemaphoreGive(_State.Mutex);
    
    esp_event_post(MODULE_EVENTS, MODULE_EVENT_CONFIG_CHANGED, 
                   p_Config, sizeof(Config_t), portMAX_DELAY);
    
    return ESP_OK;
}
```

---

## Settings Management

### Settings Structure

- Settings are organized into categories (WiFi, Display, System, etc.)
- Each category has a dedicated struct type: `App_Settings_CategoryName_t`
- Use `__attribute__((packed))` for settings structures stored in NVS
- Include reserved fields for future expansion

### Settings API Pattern

Each settings category follows this pattern:

```cpp
esp_err_t SettingsManager_GetCategory(App_Settings_Category_t* p_Settings);
esp_err_t SettingsManager_UpdateCategory(App_Settings_Category_t* p_Settings);
```

**Important**: `Update` functions modify RAM only. Call `SettingsManager_Save()` to persist changes!

### Factory Defaults

Two-tier default system:
1. **JSON Config** (`data/default_settings.json`) - Preferred, loaded on first boot
2. **Hardcoded Defaults** - Fallback in code

---

## Network and Communication

### WiFi Management

- Support both STA (Station) and AP (Access Point) modes
- Implement retry logic with configurable max attempts
- Use ESP Event system for connection state changes
- Store credentials securely in NVS

### HTTP/WebSocket Server

- Maximum clients defined by `WS_MAX_CLIENTS`
- Implement proper client tracking and cleanup
- Use WebSocket for real-time data streaming
- Regular ping intervals to detect disconnections

### VISA/SCPI Server

- Standard port: 5025
- Implement SCPI command parsing
- Return standard SCPI error codes
- Support concurrent clients (up to `VISA_MAX_CLIENTS`)

---

## Device Integration

### I2C/SPI Devices

- Initialize buses in device manager
- Create device handles for each peripheral
- Implement proper error handling and recovery
- Use appropriate clock speeds and configurations

### Lepton Camera

- Interface through custom ESP32-Lepton component
- Handle frame buffers efficiently (DMA, PSRAM)
- Implement ROI (Region of Interest) calculations
- Support multiple ROI types (Spotmeter, Scene, AGC, Video Focus)

---

## Build and Tooling

### PlatformIO Configuration

- Default environment: `debug`
- Board: `esp32-s3-devkitc-1`
- Flash size: 8MB
- PSRAM: OPI mode
- Filesystem: LittleFS

### Pre/Post Build Scripts

- `scripts/clean.py` - Clean build artifacts (pre-build)
- `scripts/format.py` - Format code with AStyle (post-build)

### Formatting

Run formatting manually:
```bash
astyle --options=scripts/astyle.cfg "main/**/*.cpp" "main/**/*.h"
```

---

## Documentation Maintenance

### AsciiDoc Documentation in `docs/`

The project maintains comprehensive AsciiDoc documentation in the `docs/` directory. **When modifying code, always update the corresponding documentation.**

#### Documentation Structure

```
docs/
├── index.adoc              # Overview and getting started
├── SettingsManager.adoc    # Settings management system
├── NetworkManager.adoc     # Network and communication
├── DeviceManager.adoc      # Device drivers and peripherals
├── TimeManager.adoc        # Time management and RTC
├── SDManager.adoc          # SD card management
├── LeptonTask.adoc        # Lepton camera task
├── GUITask.adoc           # GUI task and LVGL
├── NetworkTask.adoc       # Network task
└── VISAServer.adoc        # VISA/SCPI server
```

#### When to Update Documentation

**Always update the corresponding `.adoc` file when:**
- Adding new public API functions
- Modifying function signatures or parameters
- Changing behavior or semantics of existing functions
- Adding new modules or managers
- Modifying settings structure or event types
- Changing initialization requirements or dependencies
- Adding new features or capabilities
- Fixing bugs that affect documented behavior

#### Documentation Update Pattern

1. **Update Code First**: Make your code changes
2. **Update Documentation**: Modify the relevant `.adoc` file(s)
3. **Keep in Sync**: Ensure examples, function signatures, and descriptions match the code exactly
4. **Test Documentation**: Verify that code examples in documentation still compile and work

#### Module-to-Documentation Mapping

| Code Location | Documentation File |
|--------------|-------------------|
| `Manager/Settings/` | `SettingsManager.adoc` |
| `Manager/Network/` | `NetworkManager.adoc` |
| `Manager/Devices/` | `DeviceManager.adoc` |
| `Manager/Time/` | `TimeManager.adoc` |
| `Manager/SD/` | `SDManager.adoc` |
| `Tasks/Lepton/` | `LeptonTask.adoc` |
| `Tasks/GUI/` | `GUITask.adoc` |
| `Tasks/Network/` | `NetworkTask.adoc` |
| `Manager/Network/VISA/` | `VISAServer.adoc` |

#### Documentation Style Guidelines

- Use AsciiDoc syntax consistently
- Include code examples for new API functions
- Document all parameters, return values, and error codes
- Add diagrams for complex workflows (using PlantUML or similar)
- Keep examples realistic and tested
- Document thread-safety and RTOS considerations
- Include usage warnings and common pitfalls

**Example Documentation Entry:**
```asciidoc
=== MyModule_DoSomething()

[source,c]
----
esp_err_t MyModule_DoSomething(uint8_t *p_Data, size_t Size);
----

Performs an important operation on the provided data buffer.

**Parameters:**

* `p_Data` - Pointer to data buffer (must not be NULL)
* `Size` - Size of data buffer in bytes

**Return Values:**

* `ESP_OK` - Operation successful
* `ESP_ERR_INVALID_ARG` - NULL pointer or invalid size
* `ESP_ERR_INVALID_STATE` - Module not initialized

**Thread Safety:** This function is thread-safe and can be called from multiple tasks.

**Example:**
[source,c]
----
uint8_t Buffer[128];
esp_err_t Error = MyModule_DoSomething(Buffer, sizeof(Buffer));
if (Error != ESP_OK) {
    ESP_LOGE(TAG, "Operation failed: %d", Error);
}
----
```

#### Automated Documentation Build

Documentation is automatically built and deployed via GitHub Actions workflow (`.github/workflows/documentation.yml`). The CI/CD pipeline:
- Builds all `.adoc` files to HTML and PDF
- Deploys to GitHub Pages: https://kampi.github.io/PyroVision/
- Creates release artifacts with PDF documentation

**Do not commit generated HTML/PDF files** - these are built automatically by the CI/CD pipeline.

---

## Code Quality and Validation

### Mandatory Checks After Code Changes

**CRITICAL**: After making ANY code changes, you MUST perform the following validation steps:

#### 1. Syntax Validation
- Verify code compiles without errors using `pio run` or `idf.py build`
- Check for correct bracket matching, semicolons, and C++ syntax
- Validate all include statements and dependencies
- Ensure no missing header files or forward declarations

#### 2. Error and Warning Analysis
- **Zero tolerance for compiler warnings** - all warnings must be addressed
- Run static analysis if available
- Check for:
  - Unused variables
  - Type mismatches or implicit conversions
  - Potential null pointer dereferences
  - Memory leaks in error paths
  - Missing return statements

#### 3. Error Handling Validation
- Verify ALL ESP-IDF function return codes are checked
- Ensure proper error propagation (don't silently ignore errors)
- Validate error cleanup paths (free resources on failure)
- Check mutex/semaphore release in all paths (including errors)

#### 4. Documentation Synchronization
- Update function documentation if signatures changed
- Update parameter descriptions if behavior changed
- Verify code examples in documentation still compile
- Update relevant `.adoc` files in `docs/` directory

**Example validation checklist for each change:**
```
☐ Code compiles without errors (pio run -e debug)
☐ No compiler warnings introduced
☐ All new/modified functions have complete Doxygen documentation
☐ All return values and parameters documented
☐ Error handling implemented for all ESP-IDF calls
☐ Mutex/semaphore properly released in all code paths
☐ Related documentation (.adoc files) updated
☐ Code formatted with AStyle (scripts/format.py)
☐ Run static analysis if available (pio check)
```

### Automated Validation

Use provided scripts for validation:

```bash
# Format code
python scripts/format.py

# Build and check for errors/warnings
pio run -e debug

# Run static analysis (if configured)
pio check
```

---

## Testing and Debugging

### Debugging

- Use `ESP_LOGD` for debug output (disabled in release builds)
- Enable debug build type for verbose logging: `build_type = debug`
- Use ESP-IDF monitor with exception decoder: `monitor_filters = esp32_exception_decoder`

### Error Reporting

- Log errors with context: function name, error code, relevant parameters
- Include error strings when available
- Use descriptive error messages

---

## Best Practices

### Memory Management

- Check malloc/calloc/queue/semaphore creation success
- Free resources in deinit functions
- Use PSRAM for large buffers (frame buffers, JSON parsing)
- Be mindful of stack sizes for tasks

### Initialization Order

1. Event loop
2. NVS Flash
3. Settings Manager (loads from NVS)
4. Device Manager (I2C, SPI, peripherals)
5. Time Manager (requires RTC from Device Manager)
6. Network Manager
7. Tasks (GUI, Lepton, Network, etc.)

### Configuration

- All configurable parameters should go through Settings Manager
- Avoid hardcoded values that users might want to change
- Provide sensible defaults
- Document valid ranges and constraints

### Maintainability

- Keep functions focused and small
- Extract complex logic into separate functions
- Use descriptive variable names
- Document assumptions and constraints
- Write self-documenting code where possible

---

## Common Pitfalls to Avoid

❌ **Don't:**
- Mix tabs and spaces
- Exceed 120 character line length
- Forget error checking on ESP-IDF calls
- Omit or provide incomplete function documentation
- Skip syntax and error validation after code changes
- Ignore compiler warnings (treat warnings as errors)
- Use blocking operations in ISRs
- Forget to call `SettingsManager_Save()` after updates
- Access shared state without mutex protection

✅ **Do:**
- Use consistent naming conventions
- **Document ALL public functions with complete Doxygen comments**
- Include license headers in all files
- **Validate syntax, check for errors and warnings after every code change**
- Test error paths and edge cases
- Use appropriate log levels
- Clean up resources on failure
- Follow the established module patterns
- **Update corresponding AsciiDoc documentation when changing code**
- Address all compiler warnings before committing

---

## Additional Resources

- **ESP-IDF Documentation**: https://docs.espressif.com/projects/esp-idf/
- **FreeRTOS Documentation**: https://www.freertos.org/
- **LVGL Documentation**: https://docs.lvgl.io/
- **Project Repository**: (Add if applicable)

---

**Last Updated**: January 15, 2026  
**Maintainer**: Daniel Kampert (DanielKampert@kampis-elektroecke.de)
