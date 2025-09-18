
#pragma once
#include "sgfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sgfx_hal_cfg_spi {
  int pin_sck, pin_mosi, pin_miso, pin_cs, pin_dc, pin_rst, pin_bl;
  uint32_t hz;
} sgfx_hal_cfg_spi_t;

typedef struct sgfx_hal_cfg_i2c {
  int pin_sda, pin_scl, pin_rst, pin_bl;
  uint8_t addr;
  uint32_t hz;
} sgfx_hal_cfg_i2c_t;

/* Factories implemented by HALs */
int sgfx_hal_make_spi(sgfx_bus_t* out, const sgfx_hal_cfg_spi_t* cfg);
int sgfx_hal_make_i2c(sgfx_bus_t* out, const sgfx_hal_cfg_i2c_t* cfg);

#ifdef __cplusplus
}
#endif
