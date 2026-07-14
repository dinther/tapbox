![Component badge](https://components.espressif.com/components/docwilco/esp_abl_link/badge.svg)

# esp_abl_link

An ESP-IDF component that provides [Ableton Link](https://github.com/Ableton/link) synchronization for ESP32 devices, using Link's official C wrapper (`abl_link`).

## Overview

Ableton Link is a technology that synchronizes musical beat, tempo, and phase across multiple applications and devices connected on the same network. This component allows ESP32 devices to participate in Link sessions alongside desktop applications (like Ableton Live), mobile apps, and other Link-enabled hardware.

## Installation

### Using ESP-IDF Component Manager

Add the component to your project:

```bash
idf.py add-dependency "docwilco/esp_abl_link"
```

### Manual Installation

1. Clone this repository into your project's `components` directory:

   ```bash
   cd your_project/components
   git clone --recursive https://github.com/docwilco/esp_abl_link.git
   ```

2. If you forgot `--recursive`, initialize the submodule:

   ```bash
   cd esp_abl_link
   git submodule update --init --recursive
   ```

## Versioning

This component's version tracks the bundled Ableton Link version using the [ESP-IDF revision field](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/versioning.html):

- **Major.Minor.Patch**: Matches the Ableton Link version
- **~N**: Component revision for downstream-only changes

For example:
| Component Version | Ableton Link Version | Notes |
|-------------------|----------------------|-------|
| 3.1.5 | Link 3.1.5 | Initial release with Link 3.1.5 |
| 3.1.5~1 | Link 3.1.5 | Component-only fix |
| 3.1.6 | Link 3.1.6 | Updated to Link 3.1.6 |

## Dependencies

- ESP-IDF v5.x
- [asio](https://components.espressif.com/components/espressif/asio) component (~1.32.0)

## Configuration

This component provide a Kconfig option accessible via `idf.py menuconfig` under `Component config` > `Ableton Link`:

### `CONFIG_LINK_ESP_TASK_CORE_ID`

Selects which CPU core to run Link tasks on (only available when `FREERTOS_UNICORE` is disabled):

| Value | Description |
|-------|-------------|
| `-1`  | No affinity (`tskNO_AFFINITY`) - FreeRTOS schedules on any core (default) |
| `0`   | Core 0 - Note that WiFi also runs on core 0, which may cause contention |
| `1`   | Core 1 - Recommended to avoid contention with WiFi |

See also: [ESP-IDF FreeRTOS Task Creation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html#creation)

There is a second `link` task created, but it runs with `tskIDLE_PRIORITY`, so it does not need core affinity configuration.

## Usage

Include the header in your code:

```c
#include "abl_link.h"
```

Basic example:

```c
#include "abl_link.h"

// Create a Link instance with initial tempo of 120 BPM
abl_link link = abl_link_create(120.0);

// Enable Link to start discovering and syncing with peers
abl_link_enable(link, true);

// Capture the current session state
abl_link_session_state session_state = abl_link_create_session_state();
abl_link_capture_audio_session_state(link, session_state);

// Get the current tempo
double tempo = abl_link_tempo(session_state);

// Get beat position at a given time
uint64_t time_us = esp_timer_get_time();
double beat = abl_link_beat_at_time(session_state, time_us, 4.0);

// Clean up
abl_link_destroy_session_state(session_state);
abl_link_destroy(link);
```

Refer to the [Ableton Link C API documentation](https://github.com/Ableton/link/blob/master/extensions/abl_link/include/abl_link.h) for the full API reference.
Refer to [Link Concepts](https://ableton.github.io/link/#link-concepts) for understanding Link's architecture and terminology.

## License

This component is licensed under the **GNU General Public License v2.0 or later** (GPL-2.0-or-later).

The bundled [Ableton Link](https://github.com/Ableton/link) library is also licensed under GPL-2.0.

See [LICENSE.md](LICENSE.md) and [GNU-GPL-v2.0.md](GNU-GPL-v2.0.md) for details.

## Why C instead of C++?

I am also working on a Rust crate for Ableton Link on ESP32, which uses this C wrapper under the hood.

Since Rust's FFI does not support C++ currently, the C wrapper is the best option.
