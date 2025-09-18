# SGFX – Portable Graphics Library

A tiny, **hardware-agnostic** 2D graphics library designed to run on many MCUs (ESP32/S2/S3/C3/C6, RP2040, STM32, nRF52, …) and many display **modules** (SSD1306, ST7789, …) using only **build flags** to switch targets. This README explains how SGFX is built, how to use it, and how to add your own boards, HALs, and drivers.


## Goals

- **Agnostic by design**: same app code runs on different MCUs and display modules.
- **Pluggable layers**: Bus HAL (I2C/SPI), Display Driver (SSD1306/ST77xx/…), and Portable Core.
- **Tiny memory footprint**: small scratch buffer for staging pixels; optional streaming.
- **Simple API** for drawing pixels/lines/rects, text, blitting, clipping, rotation.
- **One demo, many targets**: select target with build flags only.


## Architecture Overview

SGFX is split into three layers:

1. **Core** (`core/`): device object, drawing primitives, text, blit helpers.
2. **Drivers** (`drivers/`): chip-specific implementations (e.g., `ssd1306.c`, `st7789.c`).
3. **HAL (Bus)** (`hal/<platform>/`): MCU/platform-specific I2C/SPI wrappers:
   - `hal/arduino/*` – generic Arduino (all supported boards)
   - `hal/espidf/*` – native ESP-IDF
   - `hal/stm32/*` – STM32Cube/HAL
   - Add others (nRF, Zephyr, bare-metal) following the same pattern.

Each device at runtime is described by a single `sgfx_device_t` that bundles:

- `caps`: width, height, color depth/format, rotation support, etc.
- `drv`: pointers to driver ops (set_window, write_pixels, fill_rect, set_rotation, present, …).
- `bus`: I2C/SPI handle via HAL (ops: begin, write_cmd, write_data, xmit, delay, …).
- `clip`: current clip rect (honored by all primitives & blits).
- `scratch`: optional staging buffer (e.g., for RGB565 batching or MONO1 building).


## Core API (high level)

```c
int sgfx_init(sgfx_device_t* dev, sgfx_bus_t* bus,
              const sgfx_driver_ops_t* drv, const sgfx_caps_t* caps,
              void* scratch, size_t scratch_bytes);

/* Device control */
void sgfx_set_clip(sgfx_device_t* d, sgfx_rect_t r);
void sgfx_reset_clip(sgfx_device_t* d);
void sgfx_set_rotation(sgfx_device_t* d, uint8_t rot_0_3);
void sgfx_set_palette(sgfx_device_t* d, const sgfx_palette_t* pal); /* mono */
void sgfx_set_dither(sgfx_device_t* d, uint8_t mode);                /* optional */
int  sgfx_present(sgfx_device_t* d);                                  /* flush */

/* Primitives */
int sgfx_clear(sgfx_device_t* d, sgfx_rgba8_t color);
int sgfx_draw_pixel(sgfx_device_t* d, int x,int y, sgfx_rgba8_t c);
int sgfx_fill_rect(sgfx_device_t* d, int x,int y,int w,int h, sgfx_rgba8_t c);
int sgfx_draw_rect(sgfx_device_t* d, int x,int y,int w,int h, sgfx_rgba8_t c);
int sgfx_draw_fast_hline(sgfx_device_t* d, int x,int y,int w, sgfx_rgba8_t c);
int sgfx_draw_fast_vline(sgfx_device_t* d, int x,int y,int h, sgfx_rgba8_t c);

/* Blit */
int sgfx_blit(sgfx_device_t* d, int x,int y, int w,int h,
              sgfx_pixfmt_t src_fmt, const void* pixels, size_t pitch_bytes);

/* Text (built-in) */
int sgfx_text5x7(sgfx_device_t* d, int x,int y, const char* s, sgfx_rgba8_t c);
int sgfx_text8x8(sgfx_device_t* d, int x,int y, const char* s, sgfx_rgba8_t c);

/* Text scaling (new) */
int sgfx_text5x7_scaled(sgfx_device_t* d, int x,int y, const char* s, sgfx_rgba8_t c, int sx,int sy);
int sgfx_text8x8_scaled(sgfx_device_t* d, int x,int y, const char* s, sgfx_rgba8_t c, int sx,int sy);
int sgfx_text5x7_width(const char* s, int sx);
int sgfx_text5x7_height(int sy);
int sgfx_text8x8_width(const char* s, int sx);
int sgfx_text8x8_height(int sy);
```

> **Note**: If you defined `SGFX_HAVE_FONT8X8`, SGFX uses the upstream dhepper 8×8 LSB-left row font. Otherwise it uses a small internal subset or your provided table. For 5×7, SGFX ships a curated ASCII set with fallback to 8×8 crop.


## Pixel Formats

- `SGFX_FMT_MONO1` – page-oriented **1bpp** as used by SSD1306 (8 vertical pixels per byte, pitch = panel width in bytes).
- `SGFX_FMT_RGB565` – 16-bit (5:6:5) packed little-endian per pixel; typical for color drivers (ST77xx).

Drivers advertise their native format via `caps` and translate input if needed (many accept RGB565 and convert internally).


## Rotation & Clipping

- Rotation is handled by the **driver** (e.g., ST7789 MADCTL). Use `sgfx_set_rotation(d, 0..3)`.
- Clipping is done in core and respected by all primitives and blits.
- The demo includes a rotation sweep and **restores** the build-time rotation afterwards.


## Memory Model & Scratch Buffer

- SGFX does not require a full-framebuffer. Most ops stream via `set_window + write_pixels`.
- Some helpers (clear/fill, RGB565 blit, mono sprite build) use the **scratch buffer** to batch pixels.
- Configure via `-DSGFX_SCRATCH_BYTES=<N>`. Examples: 1024 for mono displays, 4096 for color.


## Building & Selecting Targets

SGFX is intended to be used with **PlatformIO** (Arduino/ESP-IDF/STM32Cube), but it’s portable to other build systems. You switch **MCU** and **Module** with **build flags** only.

### Minimal example environments

```ini
;- ESP32-S3 + SSD1306 (I2C)-
[env:esp32s3-ssd1306]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
build_flags =
  -DSGFX_BUS_I2C=1
  -DSGFX_DRV_SSD1306=1
  -DSGFX_W=128 -DSGFX_H=64 -DSGFX_ROT=0
  -DSGFX_PIN_SDA=8 -DSGFX_PIN_SCL=9
  -DSGFX_I2C_ADDR=0x3C -DSGFX_I2C_HZ=400000
  -DSGFX_HAL_ARDUINO_GENERIC=1
  -DSGFX_SCRATCH_BYTES=1024

;- ESP32-S3 + ST7789 (SPI 240x135)-
[env:esp32s3-st7789]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
build_flags =
  -DSGFX_BUS_SPI=1
  -DSGFX_DRV_ST7789=1
  -DSGFX_W=240 -DSGFX_H=135 -DSGFX_ROT=1
  -DSGFX_PIN_SCK=36 -DSGFX_PIN_MOSI=35 -DSGFX_PIN_MISO=-1
  -DSGFX_PIN_CS=37  -DSGFX_PIN_DC=34  -DSGFX_PIN_RST=33 -DSGFX_PIN_BL=38
  -DSGFX_SPI_HZ=40000000
  -DSGFX_ST7789_COLSTART=0   ; adjust per module/rotation
  -DSGFX_ST7789_ROWSTART=52  ; adjust per module/rotation
  -DSGFX_HAL_ARDUINO_GENERIC=1
  -DSGFX_SCRATCH_BYTES=4096

;- ESP-IDF + ST7789-
[env:esp32s3-idf-st7789]
platform = espressif32
framework = espidf
board = esp32-s3-devkitc-1
build_flags =
  -DSGFX_BUS_SPI=1
  -DSGFX_DRV_ST7789=1
  -DSGFX_W=240 -DSGFX_H=135 -DSGFX_ROT=1
  -DSGFX_PIN_SCK=36 -DSGFX_PIN_MOSI=35 -DSGFX_PIN_MISO=-1
  -DSGFX_PIN_CS=37  -DSGFX_PIN_DC=34  -DSGFX_PIN_RST=33 -DSGFX_PIN_BL=38
  -DSGFX_SPI_HZ=40000000
  -DSGFX_HAL_ESPIDF=1
  -DSGFX_SCRATCH_BYTES=4096
  -DSGFX_IDF_SPI_HOST=SPI2_HOST
```

> **Tip**: To add a similar **MCU flavor** (ESP32-C3/C6, S2, etc.), copy an env and only change pins and `board` — the demo remains identical.


## Quickstart

1. **Add SGFX** to your project (`lib/SGFX` or as a submodule).
2. Pick an env or create one by setting the HAL, bus, and driver macros.
3. Include and run the demo:

```cpp
#include "sgfx.h"
#include "sgfx_port.h"

static uint8_t scratch[4096];
static sgfx_device_t dev;

void setup(){
  sgfx_autoinit(&dev, scratch, sizeof scratch);   // based on build flags
  sgfx_clear(&dev, (sgfx_rgba8_t){0,0,0,255});
  sgfx_text5x7(&dev, 2, 2, "Hello SGFX!", (sgfx_rgba8_t){255,255,255,255});
}
```

> The **autoinit** helper wires up the right HAL, driver and capabilities using your build flags. You can also call `sgfx_init()` manually if you need custom setup.


## The Demo (what it showcases)

- **Intro** with device size/rotation, corner markers
- **Addressing** grid (center lines + border)
- **Clipping** (central window)
- **Rotation sweep** (0..3) and **restore** to build-time rotation
- **Text scaling** (5×7 and 8×8 with `sx,sy`)
- **BLIT**: tries `MONO1` then `RGB565`
- **Color/Mono fills & gradients**
- **Overlap/merge test** for page-oriented mono
- **Perf measurement** (avg ms per frame for checker fills)

File: `src/main.cpp` (already provided in this repo).


## Configuration Macros (Build Flags)

**Bus selection**
- `-DSGFX_BUS_I2C=1` or `-DSGFX_BUS_SPI=1`

**Driver selection**
- `-DSGFX_DRV_SSD1306=1` (mono 128×64 typical)
- `-DSGFX_DRV_ST7789=1` (color 240×240 / 240×135, etc.)

**Core/Caps**
- `-DSGFX_W=<px>` `-DSGFX_H=<px>` `-DSGFX_ROT=<0..3>`
- `-DSGFX_SCRATCH_BYTES=<bytes>`

**Pins & bus speed**
- I2C: `-DSGFX_PIN_SDA`, `-DSGFX_PIN_SCL`, `-DSGFX_I2C_ADDR`, `-DSGFX_I2C_HZ`
- SPI: `-DSGFX_PIN_SCK`, `-DSGFX_PIN_MOSI`, `-DSGFX_PIN_MISO`, `-DSGFX_PIN_CS`, `-DSGFX_PIN_DC`, `-DSGFX_PIN_RST`, `-DSGFX_PIN_BL`, `-DSGFX_SPI_HZ`

**Driver tuning** (examples)
- ST7789 offsets: `-DSGFX_ST7789_COLSTART=<n>`, `-DSGFX_ST7789_ROWSTART=<n>` (per-module and rotation dependent)
- ESP-IDF SPI/I2C ports: `-DSGFX_IDF_SPI_HOST=SPI2_HOST`, `-DSGFX_IDF_I2C_PORT=I2C_NUM_0`

**HAL selection**
- `-DSGFX_HAL_ARDUINO_GENERIC=1`
- `-DSGFX_HAL_ESPIDF=1`
- `-DSGFX_HAL_STM32CUBE=1`

**Demo switches**
- `-DSGFX_DEMO_FORCE_MONO1` or `-DSGFX_DEMO_FORCE_RGB565`


## Adding a New Board (same module)

1. Duplicate a similar env in `platformio.ini`.
2. Update `board` and **pins** to match your wiring.
3. Keep the same HAL/driver defines; only bus pins/speed change.
4. Build/flash the same demo — **no app changes**.

**Example (ESP32-C3 + SSD1306)**
```ini
[env:esp32c3-ssd1306]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
build_flags =
  -DSGFX_BUS_I2C=1
  -DSGFX_DRV_SSD1306=1
  -DSGFX_W=128 -DSGFX_H=64 -DSGFX_ROT=0
  -DSGFX_PIN_SDA=8 -DSGFX_PIN_SCL=9
  -DSGFX_I2C_ADDR=0x3C -DSGFX_I2C_HZ=400000
  -DSGFX_HAL_ARDUINO_GENERIC=1
  -DSGFX_SCRATCH_BYTES=1024
```


## Adding a New Module (same MCU)

You’ll usually only change **driver** flags and dimensions:

1. Pick/implement the driver (`-DSGFX_DRV_…`).
2. Set `SGFX_W/H/ROT`. For ST7789 *240×135*, common landscape `ROT=1`.
3. Set SPI pins and `SGFX_SPI_HZ` (32–80 MHz depending on MCU/board).
4. **Offsets (ST7789)**: many breakout boards and devkits require column/row start offsets. Provide
   `SGFX_ST7789_COLSTART` / `ROWSTART`. Values vary by vendor and by rotation; consult your module’s reference or vendor library.


## Writing a New HAL (Bus Backend)

Implement the small set of bus ops used by drivers:

```c
typedef struct {
  int  (*begin)(sgfx_bus_t*);
  int  (*i2c_write)(sgfx_bus_t*, uint8_t addr, const uint8_t* bytes, size_t n);
  int  (*spi_begin_tx)(sgfx_bus_t*);        // assert CS, set DC=cmd/data
  int  (*spi_end_tx)(sgfx_bus_t*);
  int  (*spi_write)(sgfx_bus_t*, const uint8_t* bytes, size_t n);
  void (*pin_mode)(int pin, int mode);
  void (*pin_write)(int pin, int level);
  void (*delay_ms)(uint32_t ms);
} sgfx_bus_ops_t;
```

Then create `sgfx_bus_t` that stores pins, frequency, and a pointer to these ops. See `hal/arduino/arduino_spi.cpp` and `hal/arduino/arduino_i2c.cpp` for a compact reference (avoid using board-specific constants like `VSPI` directly; use default `SPI` unless a host macro is provided).


## Writing a New Driver

Implement `sgfx_driver_ops_t` for your chip:

```c
typedef struct {
  int (*init)(sgfx_device_t*);
  int (*set_window)(sgfx_device_t*, int x,int y,int w,int h);
  int (*write_pixels)(sgfx_device_t*, const void* src, size_t count, sgfx_pixfmt_t fmt);
  int (*fill_rect)(sgfx_device_t*, int x,int y,int w,int h, sgfx_rgba8_t c); // optional fast path
  int (*present)(sgfx_device_t*);                   // optional
  int (*set_rotation)(sgfx_device_t*, uint8_t rot); // optional
} sgfx_driver_ops_t;
```

Driver responsibilities:

- **Reset & init sequence**: power/reset, sleep out, color mode, MADCTL, CA/RA (column/row), frame rate.
- **Rotation**: translate 0..3 to the chip’s memory access control; adjust column/row offset if the panel is not full-frame.
- **Windowing & streaming**: implement `set_window()` → chip `CASET/RASET` (or equivalent), and stream with `write_pixels()`.
- **Pixel conversion**: accept `SGFX_FMT_MONO1`/`RGB565` as appropriate. For MONO1, pack to controller’s expected order.
- **present()**: only needed for buffered drivers (e.g., some OLED modes). For streaming TFTs, return `SGFX_OK`.

**Example notes**:
- **SSD1306**: page-oriented 1bpp; use `MONO1` buffers (pitch=W). Clip-aware writes update only the needed pages/bits.
- **ST7789**: color TFT. Pay attention to: `COLSTART/ROWSTART` macros, `MADCTL` bits for rotation & RGB/BGR, and display size (240×240 vs 240×135). Provide fast `fill_rect` by streaming repeated RGB565 without per-pixel overhead.


## Text Rendering

- **5×7**: compact column-major font with strict per-pixel plotting, plus **scaled** versions via `sgfx_text5x7_scaled(d,x,y,s,c,sx,sy)`.
- **8×8**: row-major LSB-left (dhepper) or internal subset. Scaled via `sgfx_text8x8_scaled`.
- **Layout helpers**: `*_width()` and `*_height()` return pixel sizes for a string at a given scale.
- **Opaque text** (optional): fill the glyph cell (`5*sx×7*sy` or `8*sx×8*sy`) before plotting; easy to add if needed.


## Performance Tips

- Increase `SGFX_SPI_HZ` where safe (40–80 MHz on many ESP32 boards; 32–36 MHz on some STM32 bluepill setups).
- Use the driver’s **fast `fill_rect`** path for large areas.
- Batch writes: core uses `scratch` to chunk `write_pixels()`; set `SGFX_SCRATCH_BYTES` accordingly.
- Reduce per-call overhead by minimizing `set_window()` changes (draw in tiles/stripes when possible).


## Troubleshooting

- **`VSPI not declared` on ESP32-S3**: do not hardcode `VSPI`. The Arduino HAL should use `SPI` (or accept an override like `-DSGFX_ARDUINO_SPI_HOST=<n>`). The provided Arduino HAL does this already; update older sketches.
- **Wrong offsets on ST7789 (e.g., Cardputer/odd resolutions)**: set `SGFX_ST7789_COLSTART/ROWSTART` (and confirm after rotation). Vendor libraries often publish the offsets; values vary between v1.0/v1.1/ADV or different board batches.
- **Mono sprite looks scrambled**: confirm your MONO1 source buffer is **page-oriented** (8 rows/byte, pitch = panel width). SGFX’s helper shows how to build such a buffer.
- **Text mirrored** using external 8×8 fonts: dhepper table is **LSB-left** per row. If your renderer expects MSB-left, reverse bits per row or use the provided converted header.
- **No output**: check power/BL pin, reset wiring, and correct I2C address/SPI pins. Try lower bus speeds to rule out SI issues.


## Directory Layout (suggested)

```
lib/SGFX/
  include/sgfx.h            # public API
  include/sgfx_port.h       # per-platform glue helpers & autoinit
  src/core/...
  src/drivers/ssd1306.c
  src/drivers/st7789.c
  src/hal/arduino/...
  src/hal/espidf/...
  src/hal/stm32/...
  examples/demo_universal/  # the featured demo
```

## Appendix A – ST7789 Offset & Rotation Notes

Many 240×135 modules are mounted with an internal 240×320 controller window, so you must set the visible area start.

- Use `SGFX_ST7789_COLSTART`/`ROWSTART` to align the active 240×135 into the controller’s RAM. The correct values depend on panel wiring and rotation state.
- After `sgfx_set_rotation()`, the driver may need to swap or adjust these bases. SGFX’s ST7789 driver reads your macros and applies appropriate swaps for `ROT=0..3`.

If you see extra margins on the right/bottom, increase the respective start offset; if content is clipped left/top, decrease it.


## Appendix B – MONO1 Buffer Layout (SSD1306-style)

- **Pitch**: `bytes_per_row = panel_width_in_pixels` (because each byte represents a **column** in the current 8-row page).
- **Indexing**: `buf[page*W + x]` holds bits for rows `page*8 .. page*8+7` at column `x`.
- Fill as:

```c
int page = y >> 3; int bit = y & 7; buf[page*W + x] |= (1u << bit);
```


Happy hacking!
