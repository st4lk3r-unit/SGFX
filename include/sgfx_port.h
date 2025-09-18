#pragma once
/* sgfx_port.h — glue that reads PlatformIO build flags and constructs the bus+driver */
#include "sgfx.h"
#include "sgfx_hal.h"
#include <string.h>

/* --- Required basics --- */
#ifndef SGFX_W
# error "Define SGFX_W (display width) via build_flags."
#endif
#ifndef SGFX_H
# error "Define SGFX_H (display height) via build_flags."
#endif
#ifndef SGFX_ROT
# define SGFX_ROT 0
#endif

#if !defined(SGFX_BUS_SPI) && !defined(SGFX_BUS_I2C) && !defined(SGFX_BUS_8080) && !defined(SGFX_BUS_RGB)
# error "Select one bus: -DSGFX_BUS_SPI or -DSGFX_BUS_I2C (8080/RGB reserved)"
#endif

#if !defined(SGFX_DRV_ST7789) && !defined(SGFX_DRV_ST7735) && !defined(SGFX_DRV_ST7796) && !defined(SGFX_DRV_SSD1306) && !defined(SGFX_DRV_ILI9341) && !defined(SGFX_DRV_SH1107) && !defined(SGFX_DRV_GC9A01) && !defined(SGFX_DRV_UC8151)
# error "Select a display driver: e.g. -DSGFX_DRV_ST7789"
#endif

/* --- Driver registry decls --- */
#if defined(SGFX_DRV_ST7789)
  extern const sgfx_driver_ops_t sgfx_st7789_ops;
  extern const sgfx_caps_t       sgfx_st7789_caps_default;
# define SGFX__DRV_OPS  (&sgfx_st7789_ops)
# define SGFX__DRV_CAPS (&sgfx_st7789_caps_default)
#elif defined(SGFX_DRV_SSD1306)
  extern const sgfx_driver_ops_t sgfx_ssd1306_ops;
  extern const sgfx_caps_t       sgfx_ssd1306_caps_128x64;
# define SGFX__DRV_OPS  (&sgfx_ssd1306_ops)
# define SGFX__DRV_CAPS (&sgfx_ssd1306_caps_128x64)
#elif defined(SGFX_DRV_ST7735)
  /* >>> Add these lines <<< */
  extern const sgfx_driver_ops_t sgfx_st7735_ops;
  extern const sgfx_caps_t       sgfx_st7735_caps;
#define SGFX__DRV_OPS  (&sgfx_st7735_ops)
#define SGFX__DRV_CAPS (&sgfx_st7735_caps)
#elif defined(SGFX_DRV_ST7796)
  extern const sgfx_driver_ops_t sgfx_st7796_ops;
  extern const sgfx_caps_t       sgfx_st7796_caps;
#define SGFX__DRV_OPS  (&sgfx_st7796_ops)
#define SGFX__DRV_CAPS (&sgfx_st7796_caps)
#else
# error "Selected driver not yet wired in sgfx_port.h"
#endif

/* --- Autoinit convenience --- */
static inline int sgfx_autoinit(sgfx_device_t* dev, void* scratch, size_t scratch_len) {
  static sgfx_bus_t bus;
  memset(&bus, 0, sizeof bus);

#if defined(SGFX_BUS_SPI)
  sgfx_hal_cfg_spi_t cfg = {
    .pin_sck  = SGFX_PIN_SCK,
    .pin_mosi = SGFX_PIN_MOSI,
    .pin_miso = SGFX_PIN_MISO,
    .pin_cs   = SGFX_PIN_CS,
    .pin_dc   = SGFX_PIN_DC,
    .pin_rst  = SGFX_PIN_RST,
    .pin_bl   = SGFX_PIN_BL,
    .hz       = SGFX_SPI_HZ
  };
  if (sgfx_hal_make_spi(&bus, &cfg) < 0) return -1;
#elif defined(SGFX_BUS_I2C)
  sgfx_hal_cfg_i2c_t cfg = {
    .pin_sda = SGFX_PIN_SDA,
    .pin_scl = SGFX_PIN_SCL,
    .pin_rst = SGFX_PIN_RST,
    .pin_bl  = SGFX_PIN_BL,
    .addr    = SGFX_I2C_ADDR,
    .hz      = SGFX_I2C_HZ
  };
  if (sgfx_hal_make_i2c(&bus, &cfg) < 0) return -1;
#else
# error "BUS not implemented in autoinit"
#endif
  sgfx_caps_t caps = *SGFX__DRV_CAPS;
  caps.width  = SGFX_W;
  caps.height = SGFX_H;
  int rc = sgfx_init(dev, &bus, SGFX__DRV_OPS, &caps, scratch, scratch_len);
  if (!rc) sgfx_set_rotation(dev, SGFX_ROT);
  return rc;
}
