# SGFX — Agnostic Demo Template (v0.1.3)

This demo is **MCU-agnostic** and **module-agnostic** as long as your target can build with the Arduino framework.
You switch targets **only via `build_flags`** in `platformio.ini` — no code edits required.

## How it works
- `lib/SGFX/` contains the graphics core, an Arduino-generic HAL (`lib/SGFX/src/hal/arduino`), and example drivers (`ssd1306`, `st7789`, `st7735`, `st7796`).
- `include/sgfx_port.h` reads **PlatformIO `-D` flags** and constructs the correct bus (I2C/SPI) + driver automatically.
- `src/main.cpp` calls `sgfx_autoinit(...)` once; everything else is purely flags and pins.

## Quick start
Pick an env in `platformio.ini` and hit build/upload. Examples:
- `env:esp32-ssd1306` — ESP32 Devkit + 128x64 SSD1306 on I2C.
- `env:esp32-st7789` — ESP32 Devkit + 240x240 ST7789 on SPI.
- `env:rp2040-ssd1306` — Raspberry Pi Pico + SSD1306 on I2C.
- `env:rp2040-st7789` — Raspberry Pi Pico + ST7789 on SPI.
- `env:stm32-bluepill-st7789` — STM32F103 Bluepill + ST7789 on SPI.

> If your wiring differs, copy the closest env and adjust the pin macros and `*_HZ` frequency.

## Tuning
- **Rotation**: `-DSGFX_ROT=0|1|2|3`
- **Display size**: `-DSGFX_W` / `-DSGFX_H`
- **Scratch buffer**: override with `-DSGFX_SCRATCH_BYTES=<bytes>` (default 4096).  
  For 1bpp SSD1306, `1024` is enough; for 16bpp ST7789, keep `4096+` to allow tile buffers.

## Portability status
- Current HAL: **Arduino-generic**. That makes the same demo work on ESP32 / RP2040 / STM32 (Arduino core).
- To extend beyond Arduino (e.g., Pico SDK, STM32 HAL, Zephyr), add a new `lib/SGFX/src/hal/<your-hal>/` and implement
  `sgfx_hal_make_spi()` and/or `sgfx_hal_make_i2c()`. Then compile with `-DSGFX_HAL_<YOURHAL>=1` and provide a tiny
  wrapper like the Arduino one if needed.

## Notes
- The demo avoids board-specific logic; only `Serial.begin()` is compiled when `ARDUINO` is defined.
- Drivers supported here: `SSD1306` (I2C, 1bpp) and `ST7789` (SPI, RGB565).
- You can add more drivers by implementing `sgfx_driver_ops_t` and exporting `sgfx_<name>_ops` + caps.

Happy hacking.

---


## Non‑Arduino HALs (ESP‑IDF, STM32Cube)
SGFX ships additional HAL backends so the same demo can run without the Arduino core.

### ESP‑IDF
- Set `framework = espidf` and define `-DSGFX_HAL_ESPIDF=1` plus your bus/driver pins and speed.
- Implementations live in `lib/SGFX/src/hal/espidf/` and use `i2c_master_write_to_device()` and `spi_device_transmit()`.

### STM32Cube HAL
- Set `framework = stm32cube` and define `-DSGFX_HAL_STM32CUBE=1`.
- Provide a tiny `stm32_hal.h` that includes your MCU HAL header and declares:
  ```c
  extern SPI_HandleTypeDef hspi_sgfx;
  extern I2C_HandleTypeDef hi2c_sgfx;
  void sgfx_delay_ms(uint32_t ms);
  void sgfx_gpio_set_dc(int level);
  void sgfx_gpio_set_cs(int level);
  void sgfx_gpio_set_rst(int level);
  void sgfx_gpio_set_bl(int level);
  ```
  Wire those to your generated MX code (GPIO init + HAL handles).

### POSIX (optional idea)
Add a `hal/posix` that dumps frames to PPM/PNG for CI. Ask if you want me to include it.



### ESP32 family flavors (S2/S3/C3/C6/H2)
SGFX stays agnostic across the whole Espressif line. Use the right `board = ...` and pins, and pick Arduino or ESP‑IDF:
- **Arduino**: just set `-DSGFX_HAL_ARDUINO_GENERIC=1` and your pins; cores handle the SoC differences.
- **ESP‑IDF**: set `-DSGFX_HAL_ESPIDF=1`. If needed, select the controller instances:
  - `-DSGFX_IDF_SPI_HOST=SPI2_HOST` (S2/S3/C3/C6) or leave default (classic ESP32 uses `HSPI_HOST` fallback).
  - `-DSGFX_IDF_I2C_PORT=I2C_NUM_0` (or `_1` if you wired to the second controller).
The provided envs compile as-is; adjust pins to your wiring.
