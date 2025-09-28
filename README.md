# SGFX – Portable Graphics Library

A tiny, **hardware-agnostic** 2D graphics library designed to run on many MCUs (ESP32/S2/S3/C3/C6, RP2040, STM32, nRF52, …) and many display **modules** (SSD1306, ST7789, …) using only **build flags** to switch targets. This README explains how SGFX is built, how to use it, and how to add your own boards, HALs, and drivers.

**Version:** 0.1.3

## Table of Contents
- [1. Overview](#1-overview)
- [2. Architecture](#2-architecture)
- [3. Data Flow](#3-data-flow)
- [4. Memory Model](#4-memory-model)
- [5. Configuration](#5-configuration)
- [6. Board Recipes](#6-board-recipes)
- [7. Drivers & Panels](#7-drivers--panels)
- [8. API](#8-api)
- [9. Error Handling](#9-error-handling)
- [10. Performance Tuning](#10-performance-tuning)
- [11. Porting Guide](#11-porting-guide)
- [12. Troubleshooting](#12-troubleshooting)

## 1. Overview
SGFX is a small C/C++ graphics core designed for MCUs. You draw into an **RGBA8888** framebuffer; the **presenter** converts and pushes only the parts of the screen that changed. This keeps RAM usage low and makes SPI/I2C panels feel snappy **without** a full display-sized RGB565 buffer.

**Targets**: Arduino, ESP-IDF, STM32 HAL, RP2040

**Hello, pixel (minimal loop)**
```c
#include "sgfx.h"

int main(void) {
    sgfx_t* g = sgfx_create();
    if (!g) return -1;

    sgfx_clear(g, 0xFF202020);                 // RGBA
    sgfx_fill_rect(g, 10, 10, 50, 30, 0xFFFFCC00);
    sgfx_present(g);                           // push only dirty tiles

    for (;;) {
        // ...update what changed...
        sgfx_present(g);
    }
}
```

## 2. Architecture

**Layers**

* **Core** (`src/core/`): drawing operations, clipping, color utils.
* **Drivers** (`src/drivers/`): SSD1306, ST77xx family. Each implements a small ops table (init, set-window, write-lines, etc.).
* **HAL** (`src/hal/`): Thin per-platform bus wrappers for SPI/I2C and pins (Arduino, ESP-IDF, STM32, RP2040).
* **Presenter** (`src/core/sgfx_present.*`): tile tracking, RGBA→RGB565 conversion, region flush.

**Design goals**

* Minimal RAM pressure, predictable performance on slow buses.
* One code path across platforms: switch targets via **defines**.

## 3. Data Flow

1. **Create device**: `sgfx_create()` wires bus + driver + presenter based on your defines.
2. **Draw**: call `sgfx_clear`, `sgfx_fill_rect`, `sgfx_draw_bitmap`, etc. The core marks changed tiles.
3. **Present**: `sgfx_present()` converts only dirty tiles to **RGB565** (or mono) line buffers and sends them to the driver.
4. **Repeat**: unchanged areas cost zero bus time.

**Pixel formats & byte order**

* **Input FB**: `RGBA8888` (byte order: **R G B A**). Alpha is currently ignored for panel output.
* **Panel bus**: `RGB565` (5-6-5, MSB-first over SPI). For ST77xx, color order may require `BGR=1`.

## 4. Memory Model

* **Framebuffer**: RGBA8888 `4 * W * H` bytes, owned by SGFX (see `sgfx_fb.h`).
  Tip: on ESP32, place in PSRAM if available.
* **Line buffer**: temporary RGB565 buffer sized by `2 * max_line_px` bytes.
* **Tiles**: small metadata per tile for dirty tracking.

**RAM budgeting examples**

```
SSD1306 128×64:    FB = 32 KB,  LB ≈ 512–2048 B (tuned by max_line_px)
ST7789  240×320:   FB = 300 KB, LB ≈ 480–4096 B
```

**Rotation vs W/H rule**

* Set `SGFX_W/H` to the **logical orientation you will draw in**.
* Use `SGFX_DEFAULT_ROTATION` to match physical mounting.
* `SGFX_COLSTART/ROWSTART` (ST77xx) offsets apply to the physical panel and combine with rotation.
* Many ST77xx boards need `SGFX_DEFAULT_BGR=1`.

## 5. Configuration

Pick one bus and one driver, specify panel size, then wire pins & speeds.

|                 What | Define                                                                        | Values / Notes                          |
| -------------------: | ----------------------------------------------------------------------------- | --------------------------------------- |
|                  Bus | `SGFX_BUS_SPI` / `SGFX_BUS_I2C`                                               | pick one                                |
|               Driver | `SGFX_DRV_*`                                                                  | `SSD1306`, `ST7735`, `ST7789`, `ST7796` |
|           Panel size | `SGFX_W`, `SGFX_H`                                                            | integers                                |
|             SPI pins | `SGFX_PIN_SCK`, `SGFX_PIN_MOSI`, `SGFX_PIN_CS`, `SGFX_PIN_DC`, `SGFX_PIN_RST` | required for SPI                        |
|             I2C pins | `SGFX_PIN_SCL`, `SGFX_PIN_SDA`, `SGFX_I2C_ADDR`                               | required for I2C                        |
|            SPI speed | `SGFX_SPI_HZ`                                                                 | e.g. `40000000`                         |
|            I2C speed | `SGFX_I2C_HZ`                                                                 | e.g. `400000`                           |
|             Rotation | `SGFX_DEFAULT_ROTATION`                                                       | `0..3`                                  |
| Color order (ST77xx) | `SGFX_DEFAULT_BGR`                                                            | `0=RGB`, `1=BGR`                        |
|               Invert | `SGFX_DEFAULT_INVERT`                                                         | `0/1`                                   |
|       ST77xx offsets | `SGFX_COLSTART`, `SGFX_ROWSTART`                                              | module-specific                         |

**Tile & presenter knobs**

* Runtime (if available in your build): `sgfx_opts_t { .max_line_px, .tile_w, .tile_h }` used with `sgfx_create_with(&opts)`.
* Otherwise: use compile-time defines like `SGFX_TILE_W`, `SGFX_TILE_H`, `SGFX_MAX_LINE_PX` (check your `sgfx_present.*`).

**Build with CMake (example)**

```cmake
cmake_minimum_required(VERSION 3.16)
project(sgfx_demo C)

add_library(sgfx
  ${CMAKE_SOURCE_DIR}/src/core/sgfx_core.c
  ${CMAKE_SOURCE_DIR}/src/core/sgfx_present.c
  ${CMAKE_SOURCE_DIR}/src/drivers/sgfx_st77xx.c
  ${CMAKE_SOURCE_DIR}/src/drivers/sgfx_ssd1306.c
  # select your HAL files for your platform:
  ${CMAKE_SOURCE_DIR}/src/hal/espidf/sgfx_hal_spi.c
)
target_include_directories(sgfx PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(demo main.c)
target_link_libraries(demo sgfx)
target_compile_definitions(demo PUBLIC
  SGFX_BUS_SPI SGFX_DRV_ST7789 SGFX_W=240 SGFX_H=320
  SGFX_PIN_SCK=36 SGFX_PIN_MOSI=35 SGFX_PIN_CS=34 SGFX_PIN_DC=33 SGFX_PIN_RST=37
  SGFX_SPI_HZ=40000000 SGFX_DEFAULT_BGR=1)
```

## 6. Board Recipes

### ESP32-S3 + ST7789 240×320 (SPI)

```ini
-D SGFX_BUS_SPI
-D SGFX_DRV_ST7789
-D SGFX_W=240 -D SGFX_H=320
-D SGFX_PIN_SCK=36 -D SGFX_PIN_MOSI=35 -D SGFX_PIN_CS=34 -D SGFX_PIN_DC=33 -D SGFX_PIN_RST=37
-D SGFX_SPI_HZ=40000000
-D SGFX_DEFAULT_ROTATION=0
-D SGFX_DEFAULT_BGR=1
-D SGFX_COLSTART=0 -D SGFX_ROWSTART=0
```

### RP2040 + SSD1306 128×64 (I2C @ 0x3C)

```ini
-D SGFX_BUS_I2C
-D SGFX_DRV_SSD1306
-D SGFX_W=128 -D SGFX_H=64
-D SGFX_PIN_SCL=5 -D SGFX_PIN_SDA=4
-D SGFX_I2C_ADDR=0x3C
-D SGFX_I2C_HZ=400000
```

### STM32F103 + ST7735 160×128 (SPI)

```ini
-D SGFX_BUS_SPI
-D SGFX_DRV_ST7735
-D SGFX_W=160 -D SGFX_H=128
-D SGFX_PIN_SCK=13 -D SGFX_PIN_MOSI=15 -D SGFX_PIN_CS=12 -D SGFX_PIN_DC=11 -D SGFX_PIN_RST=10
-D SGFX_SPI_HZ=24000000
-D SGFX_DEFAULT_ROTATION=1
-D SGFX_DEFAULT_BGR=0
-D SGFX_COLSTART=0 -D SGFX_ROWSTART=0
```

## 7. Drivers & Panels

### Capabilities

| Driver  | Color  | Max typical res | Rotation | Invert | BGR | Notes                        |
| ------- | ------ | --------------- | -------: | -----: | --: | ---------------------------- |
| SSD1306 | mono   | 128×64          |     0..3 |    yes | n/a | I2C/SPI variants             |
| ST7735  | 16-bit | 160×128         |     0..3 |    yes | yes | red/green tab offsets differ |
| ST7789  | 16-bit | 240×320         |     0..3 |    yes | yes | many boards need `BGR=1`     |
| ST7796  | 16-bit | 320×480         |     0..3 |    yes | yes | larger buffers help          |

### ST77xx Offsets (common modules)

| Module label     | COLSTART | ROWSTART | Notes             |
| ---------------- | -------: | -------: | ----------------- |
| ST7735 red tab   |        0 |        0 | some clones vary  |
| ST7735 green tab |        2 |        1 |                   |
| ST7789 240×320   |        0 |        0 | many need `BGR=1` |
| ST7796 320×480   |        0 |        0 |                   |

## 8. API

```c
// create a device (bus + driver + fb + presenter wired by defines/macros)
sgfx_t* sgfx_create(void);
// optionally: sgfx_t* sgfx_create_with(const sgfx_opts_t* opts); // if available

// draw
int sgfx_clear(sgfx_t*, uint32_t rgba);
int sgfx_fill_rect(sgfx_t*, int x, int y, int w, int h, uint32_t rgba);
int sgfx_draw_bitmap(sgfx_t*, int x, int y, const uint8_t* rgba, int w, int h);

// present (push dirty tiles)
int sgfx_present(sgfx_t*);

// destroy
void sgfx_destroy(sgfx_t*);
```

## 9. Error Handling

All functions return `SGFX_OK` (0) or a negative error:

* `SGFX_ERR_PARAM`: invalid width/height/pins or bad arguments
* `SGFX_ERR_BUS`: I/O failure on SPI/I2C
* `SGFX_ERR_NOSUP`: feature not supported by the selected driver
* `SGFX_ERR_STATE`: wrong call order or not initialized

**Enable logs**

```c
#define SGFX_LOG_DEBUG 1
#include "sgfx.h"
```

Then check init and bus prints from HAL/driver. Print return codes on failure paths.

## 10. Performance Tuning

**SPI/I2C electrical defaults**

* SPI: **MODE0**, MSB-first. Start ~24 MHz; many ST7789 tolerate up to **40 MHz** on ESP32-S3.
* I2C: **100–400 kHz**. Common SSD1306 addresses: **0x3C/0x3D**.
* Backlight (ST77xx **BL/LED**): user-controlled unless your HAL exposes a pin define.

**Heuristics**

* **Tile size**: larger tiles reduce per-tile overhead; smaller tiles reduce overdraw (good for I2C).
* **max_line_px**: bigger → faster but more RAM; aim for panel width on SPI, ~½ on I2C.
* **DMA/alignment**: some MCUs prefer 4-byte aligned buffers for SPI DMA.
* **Avoid full clears**: draw only changed regions, then `sgfx_present()`.

## 11. Porting Guide

To add a new platform/RTOS:

1. Implement a HAL with SPI/I2C send, GPIO set/clear, delay.
2. Provide pin/speed mapping from defines (`SGFX_PIN_*`, `SGFX_*_HZ`).
3. Ensure `sgfx_create()` selects your HAL via `#if` on platform macros.
4. Build a tiny example that toggles a pixel and presents.

**Multiple displays**

* If your build is per-instance (no global state), multiple `sgfx_t*` may work; otherwise assume **one display per process**.

## 12. Troubleshooting

* **White screen / nothing** → check CS/DC/RST; try `SGFX_DEFAULT_BGR=1` (ST77xx)
* **Offset image** → set `SGFX_COLSTART/ROWSTART`
* **Tearing/flicker (SPI)** → reduce `SGFX_SPI_HZ` or increase `max_line_px`
* **I2C lockups** → confirm `SGFX_I2C_ADDR` (0x3C/0x3D); start at 100–400 kHz
* **Rotated/garbled** → ensure `SGFX_W/H` matches logical orientation; verify rotation & BGR
