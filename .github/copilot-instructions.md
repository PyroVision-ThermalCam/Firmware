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
- **Array Initializers**: Always include a space after `{` and before `}`: `uint8_t Buf[2] = { 0x00, 0x01 };`
- **Boolean Negation**: Always use explicit comparison with `== false` or `== NULL` instead of `!` operator
- **Switch**: Indent case statements. Every `case` and `default` label **must** wrap its body in curly braces `{ }`, even if the body is a single statement. Intentional fallthrough (one case label immediately followed by another with no body) is the only exception.

**Switch Example:**
```cpp
switch (Value) {
    case FOO: {
        DoSomething();
        break;
    }
    case BAR: {
        DoSomethingElse();
        break;
    }
    default: {
        break;
    }
}
```

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

**Boolean Negation Examples:**
```cpp
// ✅ CORRECT: Explicit comparison
if (IsInitialized == false) {
    return ESP_ERR_INVALID_STATE;
}

if (p_Buffer == NULL) {
    return ESP_ERR_INVALID_ARG;
}

// ❌ INCORRECT: Negation operator
if (!IsInitialized) {
    return ESP_ERR_INVALID_STATE;
}

if (!p_Buffer) {
    return ESP_ERR_INVALID_ARG;
}
```

### Naming Conventions

#### Functions
- **Public API**: `ModuleName_FunctionName()` using PascalCase
  - Examples: `SettingsManager_Init()`, `NetworkManager_Connect()`, `TimeManager_SetTimezone()`
- **Private/Static**: `snake_case` with descriptive names
  - Examples: `on_WiFi_Event()`, `run_astyle()`

#### Variables
- **Local variables**: `PascalCase` — **all** local variables, including booleans and loop-adjacent variables
  - Examples: `RetryCount`, `EventGroup`, `Error`, `IsInitialized`, `IsRunning`
  - Simple single-letter loop counters (`i`, `x`, `y`) are the only exception
- **Pointers**: Prefix with `p_` (e.g., `p_Settings`, `p_Buffer`, `p_Data`)
- **Global/Static module state**: Prefix with underscore: `_State`, `_Network_Manager_State`, `_App_Context`

#### Constants and Macros
- **All UPPERCASE** with underscores: `WIFI_CONNECTED_BIT`, `NVS_NAMESPACE`, `SETTINGS_VERSION`
- Module-specific macros should include module name: `SETTINGS_NVS_NAMESPACE`, `VISA_MAX_CLIENTS`

#### Types
- **Structs/Enums**: `ModuleName_Description_t` with `_t` suffix
  - Examples: `Settings_t`, `Network_State_t`, `Settings_WiFi_t`
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

### Type Casting

**CRITICAL**: Always use C++ style casts. Never use C-style casts `(Type)value`.

#### Cast Types and Usage

##### static_cast<T>()
**Use for**: Safe, checked type conversions at compile time
- Numeric conversions: `static_cast<uint32_t>(value)`
- Pointer upcasting in inheritance hierarchies
- Explicit conversions between compatible types
- Void pointer to typed pointer (when type is known)

**Examples:**
```cpp
// Numeric conversions
uint32_t Value = static_cast<uint32_t>(floatValue);
size_t Size = static_cast<size_t>(intValue);

// Pointer conversions (type-safe)
uint8_t *p_Buffer = static_cast<uint8_t *>(heap_caps_malloc(Size, MALLOC_CAP_SPIRAM));
void *p_Data = GetData();
Settings_t *p_Settings = static_cast<Settings_t *>(p_Data);

// Enum conversions
USB_Mode_t Mode = static_cast<USB_Mode_t>(intValue);
```

##### reinterpret_cast<T>()
**Use for**: Low-level pointer/reference reinterpretation (use sparingly)
- Converting between unrelated pointer types
- Pointer to integer conversions
- Hardware register access

**Examples:**
```cpp
// Pointer to integer (for hardware address manipulation)
uintptr_t Address = reinterpret_cast<uintptr_t>(p_Register);

// Hardware register access
volatile uint32_t *p_Register = reinterpret_cast<volatile uint32_t *>(0x40000000);

// Reinterpreting data (when absolutely necessary)
uint32_t *p_IntData = reinterpret_cast<uint32_t *>(p_ByteArray);
```

##### const_cast<T>()
**Use for**: Removing or adding const/volatile qualifiers (discouraged, use only when interfacing with legacy APIs)
- Passing const data to non-const APIs (ESP-IDF legacy functions)
- Should be avoided in new code

**Examples:**
```cpp
// Interfacing with legacy API (use sparingly)
void LegacyFunction(char *p_Buffer);  // Should be const but isn't

const char *p_ConstStr = "Hello";
LegacyFunction(const_cast<char *>(p_ConstStr));  // Only if absolutely necessary

// Better approach: avoid const_cast by redesigning API
```

##### dynamic_cast<T>()
**Use for**: Safe downcasting in polymorphic class hierarchies with runtime checking
- Requires RTTI (Runtime Type Information)
- Returns nullptr for pointers or throws for references if cast fails
- Typically not used in embedded systems due to RTTI overhead

**Examples:**
```cpp
// Polymorphic type checking (rarely used in embedded)
Base *p_Base = GetObject();
Derived *p_Derived = dynamic_cast<Derived *>(p_Base);
if (p_Derived != nullptr) {
    p_Derived->DerivedMethod();
}
```

#### Cast Guidelines

**DO:**
- Use `static_cast<T>()` for most conversions
- Use `reinterpret_cast<T>()` only for hardware access or truly low-level operations
- Document why `reinterpret_cast<T>()` or `const_cast<T>()` is necessary
- Prefer redesigning APIs over using `const_cast<T>()`

**DON'T:**
- Never use C-style casts: `(uint8_t *)ptr` ❌
- Avoid `const_cast<T>()` unless interfacing with legacy code
- Don't use `dynamic_cast<T>()` without RTTI enabled
- Don't use `reinterpret_cast<T>()` for conversions that `static_cast<T>()` can handle

**Common Patterns:**

```cpp
// ✅ CORRECT: C++ style casts
uint8_t *p_Data = static_cast<uint8_t *>(malloc(Size));
size_t Length = static_cast<size_t>(strlen(p_String));
uint32_t Value = static_cast<uint32_t>(floatValue);

// ❌ INCORRECT: C-style casts
uint8_t *p_Data = (uint8_t *)malloc(Size);
size_t Length = (size_t)strlen(p_String);
uint32_t Value = (uint32_t)floatValue;
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
 *                  This function initializes the settings subsystem, opens the NVS namespace,
 *                  and attempts to load stored settings. If no settings exist, default values
 *                  are loaded from JSON or hardcoded defaults.
 *  @note           Must be called after NVS flash initialization.
 *                  This function must be called before any other SettingsManager API calls.
 *  @warning        Not thread-safe during initialization. Call once from main task.
 *  @return         ESP_OK on success
 *                  ESP_ERR_NVS_NOT_FOUND if settings namespace doesn't exist
 *                  ESP_ERR_NO_MEM if memory allocation fails
 *                  ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t SettingsManager_Init(void);
```

**Mandatory documentation elements and order:**
1. `@brief` - Short description on same line or next line, followed by detailed explanation (no blank line between)
2. `@note` - Important usage notes in a single consolidated block
3. `@warning` - Critical warnings in a single consolidated block (if applicable)
4. `@param` - Description for EACH parameter (include direction: in/out/inout if relevant)
5. `@return` - Document ALL possible return values in a single consolidated block

**CRITICAL formatting rules:**
- **NO blank lines** between @brief and description text
- **NO blank lines** between description and @note
- **NO blank lines** between @note and @warning
- **NO blank lines** between @warning and @param
- **NO blank lines** between @param and @return
- All @return values in ONE block with continuation indentation
- All @note statements in ONE block with continuation indentation
- All @warning statements in ONE block with continuation indentation
- Continuation lines aligned with 20 spaces of indentation

#### Incomplete Documentation is NOT Acceptable

❌ **Insufficient:**
```cpp
/** @brief Brief description.
 *  @param p_Param Description
 *  @return ESP_OK on success
 */
```

❌ **Wrong order (return before note):**
```cpp
/** @brief          Set WiFi credentials.
 *  @param p_SSID   SSID string
 *  @param p_Pass   Password string
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if parameters invalid
 *  @note           Changes not persisted until Save() called.
 */
```

❌ **Blank lines between blocks:**
```cpp
/** @brief          Set WiFi credentials.
 *  
 *  @param p_SSID   SSID string
 *  
 *  @return         ESP_OK on success
 *  
 *  @note           Changes not persisted.
 */
```

✅ **Complete and correctly formatted:**
```cpp
/** @brief          Set WiFi credentials and update configuration.
 *                  Updates the WiFi SSID and password in RAM and posts a SETTINGS_EVENT_WIFI_CHANGED
 *                  event. Changes are not persisted until SettingsManager_Save() is called.
 *  @note           Call SettingsManager_Save() to persist changes to NVS.
 *                  This function is thread-safe.
 *  @warning        Password is stored in plain text in NVS.
 *  @param p_SSID   Pointer to null-terminated SSID string (max 32 chars)
 *  @param p_Pass   Pointer to null-terminated password string (max 64 chars)
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_SSID or p_Pass is NULL
 *                  ESP_ERR_INVALID_ARG if strings exceed maximum length
 *                  ESP_ERR_INVALID_STATE if SettingsManager not initialized
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
/* Reset config_valid flag to allow reloading default config */
Error = nvs_set_u8(_State.NVS_Handle, "config_valid", false);
```

#### Section Grouping in Header Files

Never use decorative ASCII-art dividers (`/* === ... === */`) as section separators. Use Doxygen `@defgroup` + `@{` / `@}` group markers instead — they are both human-readable and picked up by the documentation generator.

```cpp
// ❌ INCORRECT: decorative divider, invisible to Doxygen
/* ============================================================
 * Devices Manager Error Codes  (DEVICES_ERR_BASE = 0x1000)
 * ============================================================ */

// ✅ CORRECT: Doxygen group
/** @defgroup DEVICES_ERRORS Devices Manager Error Codes
 *  @brief Error codes returned by DevicesManager functions (base: @c DEVICES_ERR_BASE = 0x1000).
 *  @{
 */

// ... defines ...

/** @} */
```

### Structure Documentation

#### Struct `@brief` Comment

Every `typedef struct` **MUST** be preceded by a `/** @brief ... */` comment that describes the purpose of the structure:

```cpp
/** @brief Internal runtime state of the Foo module.
 *         Holds all resources (task handle, event group, mutex) as well as
 *         the current configuration snapshot used by the task loop.
 */
typedef struct {
    ...
} Foo_State_t;
```

The `@brief` description follows the same rules as for functions:
- Short summary on the `@brief` line itself
- Multi-line detail on continuation lines (indented with 19 spaces: ` *  ` + 15 spaces)
- No blank lines between `@brief` and the continuation text

#### Struct Member Inline Documentation

Document every member with an inline `/**< ... */` comment. Align all `/**<` markers to a **consistent column** within the struct (choose the column that accommodates the longest type + name + spacing, aligned to the nearest 4-space boundary):

```cpp
/** @brief Internal runtime state of the Foo module.
 */
typedef struct {
    bool IsInitialized;          /**< true after Foo_Init() has been called successfully. */
    bool IsRunning;              /**< true while the FreeRTOS task is executing. */
    TaskHandle_t TaskHandle;     /**< FreeRTOS task handle; NULL before Foo_Task_Start(). */
    EventGroupHandle_t EventGroup; /**< Event group used for task synchronisation. */
    SemaphoreHandle_t Mutex;     /**< Mutex protecting shared fields in this struct. */
    uint8_t *p_Buffer;           /**< Heap-allocated working buffer; freed in Foo_Deinit(). */
} Foo_State_t;
```

**Rules:**
- Every member (without exception) has an inline `/**< ... */` comment
- Field names are written at natural indent (4 spaces + type + name) — **no extra padding to align type names**
- The `/**<` comment of **all members within the same struct or enum** starts at the **same column**
- That column must be a **multiple of 4** and must be at least 1 space after the longest `;` (or `,` for enums) in the type
- Comments are sentence-capitalised and end with a period
- Comments describe the **purpose** of the field, not just its type
- For handles and pointers also state **when they are valid** (e.g. "NULL before init")
- For boolean flags, state **what `true` means** (e.g. `/**< true while streaming. */`)

### Enum Documentation

```cpp
/** @brief Settings event identifiers.
 */
enum {
    SETTINGS_EVENT_LOADED,      /**< Settings loaded from NVS. */
    SETTINGS_EVENT_SAVED,       /**< Settings saved to NVS. */
    SETTINGS_EVENT_WIFI_CHANGED,/**< WiFi settings changed.
                                     Data contains Settings_WiFi_t. */
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
- **Register only the specific event IDs a handler actually processes.** Use a concrete event ID (e.g. `MY_EVENT_SPECIFIC`) instead of `ESP_EVENT_ANY_ID` whenever the handler only reacts to a subset of events from an event base. If exactly one event is handled, use one registration with that specific ID. If two events are handled, register the handler twice — once per specific ID. Only use `ESP_EVENT_ANY_ID` when all or nearly all events from that base require processing. When changing a registration from `ESP_EVENT_ANY_ID` to a specific ID (or splitting it into multiple registrations), **always update the corresponding `esp_event_handler_unregister` call(s) to match exactly**, otherwise the handler will not be removed on cleanup.

**Event Registration Examples:**
```cpp
// ✅ CORRECT: handler only processes SD_DETECT — register with specific ID
esp_event_handler_register(DEVICES_EVENTS, DEVICES_EVENT_SD_DETECT, on_Devices_Event_Handler, NULL);
esp_event_handler_unregister(DEVICES_EVENTS, DEVICES_EVENT_SD_DETECT, on_Devices_Event_Handler);

// ✅ CORRECT: handler processes two events — two separate registrations
esp_event_handler_register(GUI_TASK_EVENTS, GUI_TASK_EVENT_THERMAL_IMAGE_SAVED, on_GUI_Task_Event_Handler, NULL);
esp_event_handler_register(GUI_TASK_EVENTS, GUI_TASK_EVENT_THERMAL_IMAGE_SAVE_FAILED, on_GUI_Task_Event_Handler, NULL);
esp_event_handler_unregister(GUI_TASK_EVENTS, GUI_TASK_EVENT_THERMAL_IMAGE_SAVED, on_GUI_Task_Event_Handler);
esp_event_handler_unregister(GUI_TASK_EVENTS, GUI_TASK_EVENT_THERMAL_IMAGE_SAVE_FAILED, on_GUI_Task_Event_Handler);

// ✅ CORRECT: handler processes many events from this base — ANY_ID is justified
esp_event_handler_register(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler, NULL);
esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);

// ❌ INCORRECT: handler only uses SD_DETECT but registers for ALL device events
esp_event_handler_register(DEVICES_EVENTS, ESP_EVENT_ANY_ID, on_Devices_Event_Handler, NULL);
```

### FreeRTOS

- Task names should be descriptive: `"DevicesTask"`, `"NetworkTask"`
- Use appropriate priorities (defined in task headers)
- Always check if queue/semaphore creation succeeded
- Use `portMAX_DELAY` for blocking operations unless timeout is critical
- Prefer queues for inter-task communication
- **Always use `pdMS_TO_TICKS()` for time conversions** instead of manual division by `portTICK_PERIOD_MS`
  - For ticks-to-seconds conversion use `xTaskGetTickCount() / configTICK_RATE_HZ`

**Time Conversion Examples:**
```cpp
// ✅ CORRECT: pdMS_TO_TICKS macro
vTaskDelay(pdMS_TO_TICKS(100));
xSemaphoreTake(Mutex, pdMS_TO_TICKS(500));
xQueueReceive(Queue, &Data, pdMS_TO_TICKS(200));

// ✅ CORRECT: Ticks to seconds
uint32_t UptimeSeconds = xTaskGetTickCount() / configTICK_RATE_HZ;

// ❌ INCORRECT: Manual division by portTICK_PERIOD_MS
vTaskDelay(100 / portTICK_PERIOD_MS);
xSemaphoreTake(Mutex, 500 / portTICK_PERIOD_MS);
uint32_t Seconds = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
```

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
    if (_State.IsInitialized == false) {
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

### Module-Specific Error Codes

**CRITICAL**: Manager modules MUST return module-specific error codes instead of generic ESP-IDF error codes for domain-level failures.

#### Rules

- Define a module error base constant: `#define MODULE_ERR_BASE 0xXXXX`
- Define specific codes for all failure scenarios the module can encounter
- Return `MEMORY_ERR_INVALID_ARG`, `MEMORY_ERR_INVALID_STATE`, etc. — **never** `ESP_ERR_INVALID_ARG` or `ESP_ERR_INVALID_STATE`
- Pass-through errors from underlying ESP-IDF library calls (e.g. `esp_partition_read`) may remain as-is, since they carry diagnostic detail not expressible by module codes
- Update Doxygen `@return` documentation to list `MODULE_ERR_*` codes — never `ESP_ERR_*` variants for module-level checks

**Standard error codes to define for every Manager module:**

| Code suffix | Offset | Usage |
|-------------|--------|-------|
| `_NOT_INITIALIZED` | `+0x01` | `Init()` not yet called |
| `_INVALID_ARG` | `+0x08` | NULL pointer or out-of-range parameter |
| `_INVALID_STATE` | `+0x09` | Precondition not met (wrong state, wrong location) |
| `_NOT_FOUND` | `+0x0A` | Requested resource or partition not found |

**Example — correct pattern:**

```cpp
// ✅ CORRECT: module-specific error codes
esp_err_t Module_GetData(Data_t *p_Data)
{
    if (p_Data == NULL) {
        return MODULE_ERR_INVALID_ARG;
    }

    if (_State.IsInitialized == false) {
        return MODULE_ERR_INVALID_STATE;
    }

    // ...
}

// ❌ INCORRECT: generic ESP-IDF error codes for module-level checks
esp_err_t Module_GetData(Data_t *p_Data)
{
    if (p_Data == NULL) {
        return ESP_ERR_INVALID_ARG;   // ❌ — must use MODULE_ERR_INVALID_ARG
    }

    if (_State.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE; // ❌ — must use MODULE_ERR_INVALID_STATE
    }
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

## UI Language

**CRITICAL**: All user-facing strings in the GUI **MUST be in English**. This applies to:

- Menu entry labels (e.g., `"Calibration"` not `"Kalibrierung"`)
- Button texts
- Section labels and descriptions
- Info/warning/hint texts shown in the UI
- Dropdown options
- Any `lv_label_set_text()` / `lv_btn_create()` label content visible to the user

Comments and log messages follow the same rule — they must also be in English.

**Examples:**
```cpp
// ✅ CORRECT: English UI strings
lv_label_set_text(label, "Calibration");
lv_label_set_text(desc, "Current ambient temperature");
lv_label_set_text(btn_label, "Connect WiFi");

// ❌ INCORRECT: Non-English UI strings
lv_label_set_text(label, "Kalibrierung");
lv_label_set_text(desc, "Aktuelle Umgebungstemperatur");
```

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

**MANDATORY WORKFLOW**: When making ANY code changes that affect public APIs or module behavior, you MUST update the documentation simultaneously.

1. **Identify Affected Documentation**: Use the Module-to-Documentation Mapping table above to find the corresponding `.adoc` file(s)
2. **Update Code First**: Make your code changes in the source files
3. **Update Documentation Immediately**: In the SAME session/commit, update the `.adoc` file(s):
   - Update function signatures if changed
   - Update parameter descriptions
   - Update return value documentation
   - Update code examples to match new behavior
   - Add new sections for new features
   - Update diagrams if architecture changed
4. **Verify Consistency**: Ensure documentation exactly matches the code
5. **Test Examples**: Verify that code examples in documentation still compile and work

**CRITICAL**: Do NOT defer documentation updates to a later time. Documentation MUST be updated in the same editing session as the code change.

#### Automatic Documentation Update Rules

When you modify code, you MUST automatically update the corresponding AsciiDoc documentation according to these rules:

**1. Function Signature Changes:**
```cpp
// If you change this in the header:
esp_err_t MyModule_DoSomething(uint8_t *p_Data, size_t Size, bool NewParam);
```

**You MUST update the corresponding `.adoc` file:**
```asciidoc
=== MyModule_DoSomething()

[source,c]
----
esp_err_t MyModule_DoSomething(uint8_t *p_Data, size_t Size, bool NewParam);
----

**Parameters:**

* `p_Data` - Pointer to data buffer (must not be NULL)
* `Size` - Size of data buffer in bytes
* `NewParam` - [ADD DESCRIPTION OF NEW PARAMETER]

[Rest of documentation...]
----
```

**2. Adding New Functions:**

When adding a new public API function, you MUST add a complete documentation section to the appropriate `.adoc` file:

```asciidoc
=== NewModule_NewFunction()

[source,c]
----
esp_err_t NewModule_NewFunction(void);
----

[Brief description of what the function does]

**Return Values:**

* `ESP_OK` - Success
* [List all possible return values]

**Thread Safety:** [Describe thread-safety characteristics]

**Example:**
[source,c]
----
[Provide working code example]
----
```

**3. Behavior Changes:**

If you change how a function behaves (even without changing the signature), update the description and notes in the `.adoc` file.

**4. New Events or Types:**

When adding new event types, data structures, or enums, document them in the appropriate sections of the `.adoc` file.

#### Documentation Update Checklist

After every code change that affects public APIs, verify:

```
☐ Identified corresponding .adoc file(s) using Module-to-Documentation Mapping
☐ Updated function signatures in documentation to match code
☐ Updated or added parameter descriptions
☐ Updated or added return value documentation
☐ Updated code examples to reflect changes
☐ Added new sections for new functions/features
☐ Updated architecture diagrams if structure changed
☐ Verified consistency between code and documentation
☐ Checked that examples compile and run correctly
```

#### Common Documentation Scenarios

**Scenario 1: Adding a new parameter to an existing function**

1. Update header file with new parameter + Doxygen documentation
2. Open corresponding `.adoc` file
3. Find the function's documentation section
4. Update the function signature in the `[source,c]` block
5. Add the new parameter to the "Parameters" list
6. Update any example code to include the new parameter

**Scenario 2: Adding a completely new module**

1. Create the new module source files
2. Create a new `.adoc` file in `docs/` (e.g., `docs/NewModule.adoc`)
3. Use existing module documentation as a template (copy structure from `SettingsManager.adoc` or similar)
4. Document all public APIs, data structures, and usage examples
5. Add a link to the new documentation in `docs/index.adoc`
6. Update the Module-to-Documentation Mapping in this file

**Scenario 3: Changing function behavior without signature change**

1. Modify the function implementation
2. Open corresponding `.adoc` file
3. Update the function's description to reflect new behavior
4. Update notes, warnings, or examples as needed
5. Add version information if significant change ("*Changed in v1.1.0:* ...")

**Scenario 4: Removing or deprecating a function**

1. Mark function as deprecated in header (if deprecating) or remove (if deleting)
2. Update `.adoc` file:
   - If deprecating: Add a "**DEPRECATED**" notice and suggest alternative
   - If removing: Delete the function's documentation section entirely
3. Update examples that used the removed function

#### Module-to-Documentation Mapping

| Code Location | Documentation File |
|--------------|-------------------|
| `Manager/Settings/` | `SettingsManager.adoc` |
| `Manager/Network/` | `NetworkManager.adoc` |
| `Manager/Network/Server/HTTP/` | `HTTPServer.adoc` |
| `Manager/Network/Server/VISA/` | `VISAServer.adoc` |
| `Manager/Network/Server/WebSocket/` | `HTTPServer.adoc` (WebSocket section) |
| `Manager/Devices/` | `DeviceManager.adoc` |
| `Manager/Time/` | `TimeManager.adoc` |
| `Manager/Memory/` | `MemoryManager.adoc` |
| `Manager/USB/` | `USBManager.adoc` |
| `Tasks/Lepton/` | `LeptonTask.adoc` |
| `Tasks/GUI/` | `GUITask.adoc` |
| `Tasks/Network/` | `NetworkTask.adoc` |
| `Tasks/Devices/` | `DevicesTask.adoc` |
| `main.cpp` (main application) | `index.adoc` (overview section) |
| Any file with `heap_caps_malloc` / `malloc` / task stack | `MemoryMap.adoc` |

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

## Memory Management Documentation

### Overview

The file `docs/MemoryMap.adoc` is the **single source of truth** for all memory
allocations, task stacks, and DMA-relevant buffers in the firmware.
It must be kept in sync with the code at all times.

### ESP32-S3 Memory Regions (Quick Reference)

| Region | DMA-capable? | Notes |
|--------|-------------|-------|
| Internal SRAM (`0x3FC88000`–`0x3FD00000`) | **YES** | Use for SPI buffers, task stacks |
| PSRAM (`0x3C000000`+) | **NO** | Requires SPI bounce buffers for DMA |

**SPI Bounce Buffer Rule:** Every PSRAM buffer used as SPI TX source causes the SPI
driver to allocate an internal DMA bounce buffer of `CONFIG_SPI_TRANSFER_SIZE` (= 4,096 bytes)
per queued transaction. With `trans_queue_depth=3` this is 12,288 bytes of internal DMA RAM.
Do **NOT** increase `trans_queue_depth` above 6 without recalculating DMA RAM usage.

### When to Update `docs/MemoryMap.adoc`

**MANDATORY**: Update `docs/MemoryMap.adoc` in the **same session** as the code change
whenever any of the following happens:

- Adding a new `heap_caps_malloc()`, `malloc()`, or `calloc()` call
- Removing or resizing an existing heap allocation
- Adding a new FreeRTOS task (`xTaskCreate` / `xTaskCreatePinnedToCore`)
- Changing an existing task's stack size (`CONFIG_*_TASK_STACKSIZE`)
- Adding a new `xQueueCreate`, `xSemaphoreCreateMutex`, or `xEventGroupCreate`
- Adding buffers that indirectly use SPI DMA (i.e., PSRAM buffers passed to `spi_device_queue_trans`)

### What to Document

For every new allocation, add a row to the appropriate section of `docs/MemoryMap.adoc`:

**Heap allocations:**
```
| Buffer description + what it holds   | Size in bytes (formula preferred) | PSRAM or Internal | file:line |
```

**Task stacks:**
```
| Task name   | Stack bytes | Priority | Core | file:line |
```

**Always:**
- State the **exact size formula** (e.g. `160 * 120 * 3 = 57,600 bytes`), not just a number
- State the **heap type**: PSRAM (`MALLOC_CAP_SPIRAM`), Internal (`MALLOC_CAP_INTERNAL`),
  DMA-capable internal (both `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`)
- State whether the allocation is **permanent** (init-time) or **temporary** (freed after use)
- For temporary allocations, state the **maximum possible size** and **when it is freed**
- After adding the entry, **recalculate the section subtotal and the summary table**

### Memory Documentation Checklist

After every code change that adds or modifies memory usage, verify:

```
☐ New/modified allocation documented in docs/MemoryMap.adoc
☐ Section subtotal recalculated (PSRAM permanent / task stacks / etc.)
☐ Summary table updated (MemoryMap.adoc bottom section)
☐ SPI DMA headroom rechecked: trans_queue_depth × 4096 < (free DMA RAM after all inits)
☐ Conditional allocations (e.g. UVC, SD) clearly marked in documentation
```

### Automatic Memory Comment in Code

Every `heap_caps_malloc` call **MUST** have an inline comment stating the allocation
size with its formula:

```cpp
// ✅ CORRECT
// RGB888 frame buffer: 160 × 120 × 3 = 57,600 bytes (PSRAM)
_State.p_Buffer = static_cast<uint8_t *>(heap_caps_malloc(160 * 120 * 3, MALLOC_CAP_SPIRAM));

// ❌ INCORRECT - no size documentation
_State.p_Buffer = static_cast<uint8_t *>(heap_caps_malloc(BufferSize, MALLOC_CAP_SPIRAM));
```

If the size comes from a variable/config, document the **maximum possible value**:

```cpp
// JPEG output buffer: max = Width × Height × 3 = 160 × 120 × 3 = 57,600 bytes (PSRAM)
p_JpegBuffer = static_cast<uint8_t *>(heap_caps_malloc(JpegBufferSize, MALLOC_CAP_SPIRAM));
```

---

## Code Quality and Validation

### Mandatory Checks After Code Changes

**CRITICAL**: After making ANY code changes, you MUST perform the following validation steps:

#### 1. Syntax and Spelling Validation
- Verify code compiles without errors using `pio run` or `idf.py build`
- Check for correct bracket matching, semicolons, and C++ syntax
- Validate all include statements and dependencies
- Ensure no missing header files or forward declarations
- **Check for spelling errors** in:
  - Variable names, function names, and type names
  - Comments and documentation strings
  - String literals and user-facing messages
  - Log messages (TAG names, error messages)
- Use consistent spelling and terminology across the codebase

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
- **MANDATORY**: Update corresponding `.adoc` documentation file when changing any public API
- Use Module-to-Documentation Mapping table to identify which `.adoc` file to update
- Update function signatures if changed
- Update parameter descriptions if behavior changed
- Update return value documentation
- Verify code examples in documentation still compile
- Add new documentation sections for new functions
- Update architecture diagrams if module structure changed
- **Do NOT skip documentation updates** - they must be done in the same session as code changes

**Example validation checklist for each change:**
```
☐ Code compiles without errors (pio run -e debug)
☐ No compiler warnings introduced
☐ No spelling errors in code, comments, or documentation
☐ All new/modified functions have complete Doxygen documentation
☐ All return values and parameters documented
☐ Error handling implemented for all ESP-IDF calls
☐ Mutex/semaphore properly released in all code paths
☐ Related documentation (.adoc files) updated using Module-to-Documentation Mapping
☐ New functions have complete documentation sections in .adoc files
☐ Function signatures in .adoc files match code exactly
☐ Code examples in documentation updated and verified
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
- Use `portTICK_PERIOD_MS` for time conversions (use `pdMS_TO_TICKS()` instead)
- Use languages other than **English** for UI labels, menu entries, button texts, or any other user-facing strings

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
- **Use `pdMS_TO_TICKS()` for all millisecond-to-tick conversions**
- **Use English exclusively for all UI labels, menu entries, button texts, descriptions, and user-facing strings**

---

## Additional Resources

- **ESP-IDF Documentation**: https://docs.espressif.com/projects/esp-idf/
- **FreeRTOS Documentation**: https://www.freertos.org/
- **LVGL Documentation**: https://docs.lvgl.io/
- **Project Repository**: (Add if applicable)

---

**Last Updated**: February 26, 2026  
**Maintainer**: Daniel Kampert (DanielKampert@kampis-elektroecke.de)
