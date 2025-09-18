/* SGFX ST7796 driver (RGB565, streaming, stateless)
   - No per-device state; offsets derived from current rotation each call
   - Works in 4-wire SPI (DBI Type C)
   - Minimal, robust init + MADCTL rotation + optional inversion
*/
#ifdef SGFX_DRV_ST7796

#include "sgfx.h"
#include "sgfx_port.h"
#include <stdint.h>
#include <string.h>

#ifndef SGFX_BUS_SPI
# error "ST7796 driver requires SPI: define SGFX_BUS_SPI"
#endif

/* ======= Tunables via build flags (safe defaults) ======= */
#ifndef SGFX_ST7796_BGR
# define SGFX_ST7796_BGR 1      /* many modules are BGR */
#endif
#ifndef SGFX_ST7796_INVERT
# define SGFX_ST7796_INVERT 0   /* flip to 1 if colors look 'negative' */
#endif
#ifndef SGFX_ST7796_COLSTART
# define SGFX_ST7796_COLSTART 0 /* adjust if you see margins */
#endif
#ifndef SGFX_ST7796_ROWSTART
# define SGFX_ST7796_ROWSTART 0
#endif
#ifndef SGFX_ST7796_INIT_DELAY_MS
# define SGFX_ST7796_INIT_DELAY_MS 120
#endif

/* ======= ST7796 command set (subset) ======= */
#define ST_CMD_SWRESET  0x01
#define ST_CMD_SLPIN    0x10
#define ST_CMD_SLPOUT   0x11
#define ST_CMD_NORON    0x13
#define ST_CMD_INVOFF   0x20
#define ST_CMD_INVON    0x21
#define ST_CMD_DISPOFF  0x28
#define ST_CMD_DISPON   0x29
#define ST_CMD_CASET    0x2A
#define ST_CMD_RASET    0x2B
#define ST_CMD_RAMWR    0x2C
#define ST_CMD_MADCTL   0x36
#define ST_CMD_COLMOD   0x3A

/* MADCTL bits */
#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_BGR 0x08

/* ======= HAL hooks provided by sgfx_port ======= */
#ifndef sgfx_cmd8
  extern int  sgfx_cmd8 (sgfx_device_t*, uint8_t cmd);
#endif
#ifndef sgfx_cmdn
  extern int  sgfx_cmdn (sgfx_device_t*, uint8_t cmd, const uint8_t* data, size_t n);
#endif
#ifndef sgfx_data
  extern int  sgfx_data (sgfx_device_t*, const void* bytes, size_t n);
#endif
#ifndef sgfx_delay_ms
  extern void sgfx_delay_ms(uint32_t ms);
#endif

/* ======= Helpers ======= */
static inline uint16_t pack565(sgfx_rgba8_t c){
  return (uint16_t)(((c.r & 0xF8)<<8) | ((c.g & 0xFC)<<3) | (c.b >> 3));
}

/* Offsets swap on 90°/270° (MV) rotations */
static inline void st7796_get_ofs(uint8_t rot, uint16_t* xofs, uint16_t* yofs){
  uint16_t cs = (uint16_t)SGFX_ST7796_COLSTART;
  uint16_t rs = (uint16_t)SGFX_ST7796_ROWSTART;
  if (rot & 1){ *xofs = rs; *yofs = cs; } else { *xofs = cs; *yofs = rs; }
}

/* ======= Ops ======= */

static int st7796_set_rotation(sgfx_device_t* d, uint8_t rot){
  rot &= 3;
  uint8_t mad = 0;
  if (SGFX_ST7796_BGR) mad |= MADCTL_BGR;

  /* Common mapping used by most ST77xx/ILI9xxx families */
  switch (rot){
    case 0: mad |= (MADCTL_MX | MADCTL_MY); break;  /* portrait */
    case 1: mad |= (MADCTL_MY | MADCTL_MV); break;  /* landscape 90° */
    case 2: mad |= (0);                    break;   /* portrait 180° */
    case 3: mad |= (MADCTL_MX | MADCTL_MV); break;  /* landscape 270° */
  }
  return sgfx_cmdn(d, ST_CMD_MADCTL, &mad, 1);
}

static int st7796_set_window(sgfx_device_t* d, int x,int y,int w,int h){
  uint16_t xo, yo;
  st7796_get_ofs(d->rotation & 3, &xo, &yo);

  uint16_t x0 = (uint16_t)x + xo;
  uint16_t y0 = (uint16_t)y + yo;
  uint16_t x1 = (uint16_t)(x + w - 1) + xo;
  uint16_t y1 = (uint16_t)(y + h - 1) + yo;

  uint8_t ca[4] = { (uint8_t)(x0>>8), (uint8_t)x0, (uint8_t)(x1>>8), (uint8_t)x1 };
  uint8_t ra[4] = { (uint8_t)(y0>>8), (uint8_t)y0, (uint8_t)(y1>>8), (uint8_t)y1 };

  int rc = sgfx_cmdn(d, ST_CMD_CASET, ca, 4); if (rc) return rc;
  rc = sgfx_cmdn(d, ST_CMD_RASET, ra, 4);     if (rc) return rc;
  rc = sgfx_cmd8(d, ST_CMD_RAMWR);            if (rc) return rc;
  return SGFX_OK;
}

static int st7796_write_pixels(sgfx_device_t* d, const void* src, size_t count, sgfx_pixfmt_t fmt){
  if (fmt != SGFX_FMT_RGB565) return SGFX_ERR_NOSUP; /* stream RGB565 only */
  return sgfx_data(d, src, count * 2u);
}

static int st7796_fill_rect(sgfx_device_t* d, int x,int y,int w,int h, sgfx_rgba8_t c){
  int rc = st7796_set_window(d, x,y,w,h);
  if (rc) return rc;

  size_t total = (size_t)w * (size_t)h;
  size_t maxpx = d->scratch_bytes / 2;
  if (!maxpx) return SGFX_ERR_NOMEM;

  uint16_t* buf = (uint16_t*)d->scratch;
  uint16_t p = pack565(c);
  for (size_t i=0;i<maxpx;++i) buf[i] = p;

  while (total){
    size_t n = (total > maxpx) ? maxpx : total;
    rc = sgfx_data(d, buf, n * 2u);
    if (rc) return rc;
    total -= n;
  }
  return SGFX_OK;
}

static int st7796_present(sgfx_device_t* d){
  (void)d; return SGFX_OK; /* streaming TFT */
}

static int st7796_init(sgfx_device_t* d){
  int rc = sgfx_cmd8(d, ST_CMD_SWRESET); if (rc) return rc;
  sgfx_delay_ms(5);

  rc = sgfx_cmd8(d, ST_CMD_SLPOUT);      if (rc) return rc;
  sgfx_delay_ms(SGFX_ST7796_INIT_DELAY_MS);

  /* 16-bit color */
  { uint8_t v = 0x55; rc = sgfx_cmdn(d, ST_CMD_COLMOD, &v, 1); if (rc) return rc; }

  /* Apply build-time rotation (stored in d->rotation by core) */
  rc = st7796_set_rotation(d, d->rotation & 3); if (rc) return rc;

  /* Optional display inversion */
#if SGFX_ST7796_INVERT
  rc = sgfx_cmd8(d, ST_CMD_INVON);  if (rc) return rc;
#else
  rc = sgfx_cmd8(d, ST_CMD_INVOFF); if (rc) return rc;
#endif

  rc = sgfx_cmd8(d, ST_CMD_NORON);  if (rc) return rc; sgfx_delay_ms(10);
  rc = sgfx_cmd8(d, ST_CMD_DISPON); if (rc) return rc; sgfx_delay_ms(10);
  return SGFX_OK;
}

/* ======= Public symbols ======= */

const sgfx_caps_t sgfx_st7796_caps = {
  .width  = SGFX_W,
  .height = SGFX_H,
  /* add caps flags if your sgfx_caps_t defines them (e.g., .caps = SGFX_CAP_COLOR|SGFX_CAP_RGB565) */
};

const sgfx_driver_ops_t sgfx_st7796_ops = {
  .init         = st7796_init,
  .set_window   = st7796_set_window,
  .write_pixels = st7796_write_pixels,
  .fill_rect    = st7796_fill_rect,
  .present      = st7796_present,
  .set_rotation = st7796_set_rotation,
};

#endif /* SGFX_DRV_ST7796 */
