#include "sgfx.h"
#include <string.h>

/* -------------------- tiny helpers -------------------- */

static inline int clampi(int v, int lo, int hi){ return v<lo?lo:(v>hi?hi:v); }

static inline uint16_t pack565(sgfx_rgba8_t c){
  return (uint16_t)(((c.r & 0xF8)<<8) | ((c.g & 0xFC)<<3) | (c.b>>3));
}

/* -------------------- core init / state -------------------- */

int sgfx_init(sgfx_device_t* dev, sgfx_bus_t* bus,
              const sgfx_driver_ops_t* drv, const sgfx_caps_t* caps,
              void* scratch_buf, size_t scratch_len) {
  if (!dev || !bus || !drv || !caps) return SGFX_ERR_INVAL;
  memset(dev, 0, sizeof *dev);
  dev->bus = bus;
  dev->drv = drv;
  dev->caps = *caps;
  dev->scratch = scratch_buf;
  dev->scratch_bytes = scratch_len;
  dev->clip = (sgfx_rect_t){0,0,(int16_t)caps->width,(int16_t)caps->height};
  if (bus->ops && bus->ops->begin) bus->ops->begin(bus);
  /* default palette for mono */
  dev->palette.size = 2;
  dev->palette.colors[0] = (sgfx_rgba8_t){0,0,0,255};
  dev->palette.colors[1] = (sgfx_rgba8_t){255,255,255,255};

  if (dev->drv->init) return dev->drv->init(dev);
  return SGFX_OK;
}

void sgfx_set_clip(sgfx_device_t* d, sgfx_rect_t r){
  int16_t x = clampi(r.x, 0, d->caps.width);
  int16_t y = clampi(r.y, 0, d->caps.height);
  int16_t w = clampi(r.w, 0, d->caps.width - x);
  int16_t h = clampi(r.h, 0, d->caps.height - y);
  d->clip = (sgfx_rect_t){x,y,w,h};
}
void sgfx_reset_clip(sgfx_device_t* d){
  d->clip = (sgfx_rect_t){0,0,(int16_t)d->caps.width,(int16_t)d->caps.height};
}

void sgfx_set_rotation(sgfx_device_t* d, uint8_t rot){
  d->rotation = rot & 3;
  if (d->drv->set_rotation) d->drv->set_rotation(d, d->rotation);
}

void sgfx_set_palette(sgfx_device_t* d, const sgfx_palette_t* pal){
  if (pal) d->palette = *pal;
}
void sgfx_set_dither(sgfx_device_t* d, uint8_t mode){ d->dither = mode; }

/* -------------------- primitives -------------------- */

int sgfx_clear(sgfx_device_t* d, sgfx_rgba8_t color){
  return sgfx_fill_rect(d, 0,0, d->caps.width, d->caps.height, color);
}

int sgfx_draw_pixel(sgfx_device_t* d, int x, int y, sgfx_rgba8_t c){
  if (x < d->clip.x || y < d->clip.y ||
      x >= d->clip.x + d->clip.w || y >= d->clip.y + d->clip.h) return SGFX_OK;
  return sgfx_fill_rect(d, x, y, 1, 1, c);
}

int sgfx_fill_rect(sgfx_device_t* d, int x, int y, int w, int h, sgfx_rgba8_t c){
  /* clip */
  int x0 = clampi(x, d->clip.x, d->clip.x + d->clip.w);
  int y0 = clampi(y, d->clip.y, d->clip.y + d->clip.h);
  int x1 = clampi(x+w, d->clip.x, d->clip.x + d->clip.w);
  int y1 = clampi(y+h, d->clip.y, d->clip.y + d->clip.h);
  if (x1 <= x0 || y1 <= y0) return SGFX_OK;

  if (d->drv->fill_rect) return d->drv->fill_rect(d, x0, y0, x1-x0, y1-y0, c);

  /* generic path: set window + stream converted pixels using scratch */
  if (!d->drv->set_window || !d->drv->write_pixels) return SGFX_ERR_NOSUP;
  int rc = d->drv->set_window(d, x0, y0, x1-x0, y1-y0);
  if (rc) return rc;

  size_t count = (size_t)((x1-x0)*(y1-y0));
  size_t max_px = d->scratch_bytes / 2; /* RGB565 in scratch */
  if (max_px == 0) return SGFX_ERR_NOMEM;

  uint16_t* buf = (uint16_t*)d->scratch;
  uint16_t pix565 = pack565(c);

  while (count){
    size_t n = count > max_px ? max_px : count;
    for (size_t i=0; i<n; ++i) buf[i] = pix565;
    rc = d->drv->write_pixels(d, buf, n, SGFX_FMT_RGB565);
    if (rc) return rc;
    count -= n;
  }
  return SGFX_OK;
}

int sgfx_present(sgfx_device_t* d){
  if (d->drv->present) return d->drv->present(d);
  return SGFX_OK;
}

/* Basic blit: assumes src_fmt is RGB565 or MONO1 into device native via driver */
int sgfx_blit(sgfx_device_t* d, int x,int y, int w,int h,
              sgfx_pixfmt_t src_fmt, const void* pixels, size_t pitch_bytes){
  if (!d->drv->set_window || !d->drv->write_pixels) return SGFX_ERR_NOSUP;
  if (x >= d->clip.x + d->clip.w || y >= d->clip.y + d->clip.h) return SGFX_OK;
  int rx = x < d->clip.x ? d->clip.x - x : 0;
  int ry = y < d->clip.y ? d->clip.y - y : 0;
  int rw = (x+w) > (d->clip.x + d->clip.w) ? (d->clip.x + d->clip.w) - x : w;
  int rh = (y+h) > (d->clip.y + d->clip.h) ? (d->clip.y + d->clip.h) - y : h;
  if (rw <= 0 || rh <= 0) return SGFX_OK;

  int rc = d->drv->set_window(d, x+rx, y+ry, rw, rh);
  if (rc) return rc;

  const uint8_t* row = (const uint8_t*)pixels + ry * pitch_bytes + (src_fmt==SGFX_FMT_RGB565 ? rx*2 : rx);
  for (int j=0; j<rh; ++j){
    size_t count = (size_t)rw; /* driver interprets by src_fmt */
    rc = d->drv->write_pixels(d, row, count, src_fmt);
    if (rc) return rc;
    row += pitch_bytes;
  }
  return SGFX_OK;
}

/* -------------------- 8x8 FONT (full ASCII when available) -------------------- */
/* If available, use Daniel Hepper's public-domain 8x8 font (row-wise, LSB-left). */
#if __has_include("font8x8_basic.h")
  #include "font8x8_basic.h"  /* Provides: const uint8_t font8x8_basic[128][8]; */
  #define SGFX_HAVE_FONT8X8 1
#else
  #define SGFX_HAVE_FONT8X8 0
  /* Fallback: small internal subset (MSB-left rows) just to keep compiling.
     You can keep your old subset here; a few examples are left in to show shape.
     For full coverage, prefer font8x8_basic.h. */
  static const uint8_t sgfx_font8x8_basic_subset[96][8] = {
  /* 0x20 ' ' */ {0,0,0,0,0,0,0,0},
  /* 0x21 '!' */ {24,24,24,24,24,0,24,0},
  /* 0x22 '"'*/ {54,54,20,0,0,0,0,0},
  /* 0x23 '#'*/ {54,54,127,54,127,54,54,0},
  /* 0x24 '$'*/ {8,62,40,62,10,62,8,0},
  /* 0x25 '%'*/ {99,103,14,28,56,115,99,0},
  /* 0x26 '&'*/ {28,54,28,110,59,51,110,0},
  /* 0x27 '\''*/ {24,24,12,0,0,0,0,0},
  /* 0x28 '('*/ {12,24,48,48,48,24,12,0},
  /* 0x29 ')'*/ {48,24,12,12,12,24,48,0},
  /* 0x2A '*'*/ {0,54,28,127,28,54,0,0},
  /* 0x2B '+'*/ {0,24,24,126,24,24,0,0},
  /* 0x2C ','*/ {0,0,0,0,0,24,24,12},
  /* 0x2D '-'*/ {0,0,0,126,0,0,0,0},
  /* 0x2E '.'*/ {0,0,0,0,0,24,24,0},
  /* 0x2F '/'*/ {3,6,12,24,48,96,64,0},
  /* 0x30 '0'*/ {62,99,103,107,115,99,62,0},
  /* 0x31 '1'*/ {24,56,24,24,24,24,126,0},
  /* 0x32 '2'*/ {62,99,3,6,28,48,127,0},
  /* 0x33 '3'*/ {127,6,12,6,3,99,62,0},
  /* 0x34 '4'*/ {6,14,22,38,127,6,6,0},
  /* 0x35 '5'*/ {127,96,124,3,3,99,62,0},
  /* 0x36 '6'*/ {28,48,96,124,99,99,62,0},
  /* 0x37 '7'*/ {127,3,6,12,24,24,24,0},
  /* 0x38 '8'*/ {62,99,62,99,99,99,62,0},
  /* 0x39 '9'*/ {62,99,99,63,3,6,28,0},
  /* 0x3A ':'*/ {0,24,24,0,0,24,24,0},
  /* 0x3B ';'*/ {0,24,24,0,0,24,24,12},
  /* 0x3C '<'*/ {6,12,24,48,24,12,6,0},
  /* 0x3D '='*/ {0,0,126,0,126,0,0,0},
  /* 0x3E '>'*/ {48,24,12,6,12,24,48,0},
  /* 0x3F '?'*/ {62,99,3,6,12,0,12,0},
  /* 0x40 '@'*/ {62,99,123,123,123,96,62,0},
  /* 0x41 'A'*/ {24,60,102,102,126,102,102,0},
  /* 0x42 'B'*/ {124,102,102,124,102,102,124,0},
  /* 0x43 'C'*/ {62,99,96,96,96,99,62,0},
  /* 0x44 'D'*/ {120,108,102,102,102,108,120,0},
  /* 0x45 'E'*/ {126,96,96,120,96,96,126,0},
  /* 0x46 'F'*/ {126,96,96,120,96,96,96,0},
  };
#endif

/* Optional size helpers */
static inline int sgfx_text8x8_width(const char* s, int sx){
  if (!s || sx<=0) return 0;
  int n=0; while(*s){ ++n; ++s; }
  return n * (8 * sx);
}
static inline int sgfx_text8x8_height(int sy){ return 8 * sy; }

/* bit-reverse for MSB-left fallback tables */
static inline uint8_t sgfx_rev8(uint8_t b){
  b = (b>>4) | (b<<4);
  b = ((b & 0xCC)>>2) | ((b & 0x33)<<2);
  b = ((b & 0xAA)>>1) | ((b & 0x55)<<1);
  return b;
}

int sgfx_text8x8_scaled(sgfx_device_t* d, int x,int y,
                        const char* s, sgfx_rgba8_t c,
                        int sx, int sy)
{
  if (!s || sx<=0 || sy<=0) return SGFX_ERR_INVAL;

  int cx = x;
  for (; *s; ++s, cx += 8 * sx){
    unsigned ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) continue;

#if SGFX_HAVE_FONT8X8
    /* Upstream: row-major, LSB-left */
    const uint8_t* glyph = font8x8_basic[ch];
    for (int row=0; row<8; ++row){
      uint8_t bits = glyph[row];
      while (bits){
        int col = __builtin_ctz(bits);      /* LSB-left column index */
        sgfx_fill_rect(d, cx + col*sx, y + row*sy, sx, sy, c);
        bits &= (uint8_t)(bits-1);
      }
    }
#else
    /* Internal subset (MSB-left rows) — mirror by reversing bits */
    const uint8_t* glyph = sgfx_font8x8_basic_subset[ch-32];
    for (int row=0; row<8; ++row){
      uint8_t bits = sgfx_rev8(glyph[row]); /* now LSB-left */
      while (bits){
        int col = __builtin_ctz(bits);
        sgfx_fill_rect(d, cx + col*sx, y + row*sy, sx, sy, c);
        bits &= (uint8_t)(bits-1);
      }
    }
#endif
  }
  return SGFX_OK;
}

/* keep old entry point working */
int sgfx_text8x8(sgfx_device_t* d, int x,int y, const char* s, sgfx_rgba8_t c){
  return sgfx_text8x8_scaled(d, x, y, s, c, 1, 1);
}



int sgfx_draw_fast_hline(sgfx_device_t* d, int x,int y,int w, sgfx_rgba8_t c){
  return sgfx_fill_rect(d, x, y, w, 1, c);
}
int sgfx_draw_fast_vline(sgfx_device_t* d, int x,int y,int h, sgfx_rgba8_t c){
  return sgfx_fill_rect(d, x, y, 1, h, c);
}
int sgfx_draw_rect(sgfx_device_t* d, int x,int y,int w,int h, sgfx_rgba8_t c){
  if (w<=0||h<=0) return SGFX_OK;
  int rc=0;
  rc |= sgfx_draw_fast_hline(d, x, y, w, c);
  rc |= sgfx_draw_fast_hline(d, x, y+h-1, w, c);
  rc |= sgfx_draw_fast_vline(d, x, y, h, c);
  rc |= sgfx_draw_fast_vline(d, x+w-1, y, h, c);
  return rc;
}

/* -------------------- 5x7 FONT (caps+lower+symbols + 8x8 fallback) -------------------- */
/* Column-major (5 bytes per glyph). Bit 0..6 = rows top..bottom. */
static const char sgfx_font5x7_map[] =
  " !\"#$%&'()*+,-./"
  "0123456789:;<=>?@"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
  "abcdefghijklmnopqrstuvwxyz{|}~";

/* Minimal, readable 5x7 glyphs for the full ASCII set.
   For brevity, a curated set is provided here (caps/digits/punct/lowercase).
   Any glyph not in this table will be auto-fallback-rendered from 8x8. */
typedef struct { char ch; uint8_t col[5]; } glyph5x7_t;
static const glyph5x7_t sgfx_font5x7_glyphs[] = {
  /* Space & basic punctuation */
  {' ', {0x00,0x00,0x00,0x00,0x00}},
  {'!', {0x00,0x00,0x5F,0x00,0x00}},  /* ! */
  {'"', {0x00,0x07,0x00,0x07,0x00}},  /* " */
  {'#', {0x14,0x7F,0x14,0x7F,0x14}},  /* # */
  {'$', {0x24,0x2A,0x7F,0x2A,0x12}},  /* $ */
  {'%', {0x23,0x13,0x08,0x64,0x62}},  /* % */
  {'&', {0x36,0x49,0x55,0x22,0x50}},  /* & */
  {'\'',{0x00,0x05,0x03,0x00,0x00}},  /* ' */
  {'(', {0x00,0x1C,0x22,0x41,0x00}},  /* ( */
  {')', {0x00,0x41,0x22,0x1C,0x00}},  /* ) */
  {'*', {0x14,0x08,0x3E,0x08,0x14}},  /* * */
  {'+', {0x08,0x08,0x3E,0x08,0x08}},  /* + */
  {',', {0x00,0x50,0x30,0x00,0x00}},  /* , */
  {'-', {0x08,0x08,0x08,0x08,0x08}},  /* - */
  {'.', {0x00,0x60,0x60,0x00,0x00}},  /* . */
  {'/', {0x20,0x10,0x08,0x04,0x02}},  /* / */

  /* Digits */
  {'0', {0x3E,0x51,0x49,0x45,0x3E}},
  {'1', {0x00,0x42,0x7F,0x40,0x00}},
  {'2', {0x62,0x51,0x49,0x49,0x46}},
  {'3', {0x22,0x49,0x49,0x49,0x36}},
  {'4', {0x18,0x14,0x12,0x7F,0x10}},
  {'5', {0x2F,0x49,0x49,0x49,0x31}},
  {'6', {0x3E,0x49,0x49,0x49,0x32}},
  {'7', {0x01,0x71,0x09,0x05,0x03}},
  {'8', {0x36,0x49,0x49,0x49,0x36}},
  {'9', {0x26,0x49,0x49,0x49,0x3E}},

  /* Punctuation cont. */
  {':', {0x00,0x36,0x36,0x00,0x00}},
  {';', {0x00,0x56,0x36,0x00,0x00}},
  {'<', {0x08,0x14,0x22,0x41,0x00}},
  {'=', {0x14,0x14,0x14,0x14,0x14}},
  {'>', {0x00,0x41,0x22,0x14,0x08}},
  {'?', {0x02,0x01,0x59,0x09,0x06}},
  {'@', {0x3E,0x41,0x5D,0x55,0x1E}},

  /* Uppercase A–Z (classic 5x7) */
  {'A', {0x7E,0x11,0x11,0x11,0x7E}},
  {'B', {0x7F,0x49,0x49,0x49,0x36}},
  {'C', {0x3E,0x41,0x41,0x41,0x22}},
  {'D', {0x7F,0x41,0x41,0x22,0x1C}},
  {'E', {0x7F,0x49,0x49,0x49,0x41}},
  {'F', {0x7F,0x09,0x09,0x09,0x01}},
  {'G', {0x3E,0x41,0x49,0x49,0x7A}},
  {'H', {0x7F,0x08,0x08,0x08,0x7F}},
  {'I', {0x41,0x41,0x7F,0x41,0x41}},
  {'J', {0x20,0x40,0x41,0x3F,0x01}},
  {'K', {0x7F,0x08,0x14,0x22,0x41}},
  {'L', {0x7F,0x40,0x40,0x40,0x40}},
  {'M', {0x7F,0x02,0x0C,0x02,0x7F}},
  {'N', {0x7F,0x04,0x08,0x10,0x7F}},
  {'O', {0x3E,0x41,0x41,0x41,0x3E}},
  {'P', {0x7F,0x09,0x09,0x09,0x06}},
  {'Q', {0x3E,0x41,0x51,0x21,0x5E}},
  {'R', {0x7F,0x09,0x19,0x29,0x46}},
  {'S', {0x26,0x49,0x49,0x49,0x32}},
  {'T', {0x01,0x01,0x7F,0x01,0x01}},
  {'U', {0x3F,0x40,0x40,0x40,0x3F}},
  {'V', {0x1F,0x20,0x40,0x20,0x1F}},
  {'W', {0x7F,0x20,0x18,0x20,0x7F}},
  {'X', {0x63,0x14,0x08,0x14,0x63}},
  {'Y', {0x07,0x08,0x70,0x08,0x07}},
  {'Z', {0x61,0x51,0x49,0x45,0x43}},

  /* Brackets & friends */
  {'[', {0x00,0x7F,0x41,0x41,0x00}},
  {'\\',{0x02,0x04,0x08,0x10,0x20}},
  {']', {0x00,0x41,0x41,0x7F,0x00}},
  {'^', {0x04,0x02,0x01,0x02,0x04}},
  {'_', {0x40,0x40,0x40,0x40,0x40}},
  {'`', {0x00,0x01,0x02,0x04,0x00}},

  /* Lowercase a–z (no descenders beyond row 6 to stay 5x7) */
  {'a', {0x20,0x54,0x54,0x54,0x78}},
  {'b', {0x7F,0x44,0x44,0x44,0x38}},
  {'c', {0x38,0x44,0x44,0x44,0x28}},
  {'d', {0x38,0x44,0x44,0x44,0x7F}},
  {'e', {0x38,0x54,0x54,0x54,0x18}},
  {'f', {0x08,0x7E,0x09,0x01,0x02}},
  {'g', {0x08,0x54,0x54,0x54,0x3C}}, /* simple tail-less g */
  {'h', {0x7F,0x04,0x04,0x04,0x78}},
  {'i', {0x00,0x44,0x7D,0x40,0x00}},
  {'j', {0x20,0x40,0x44,0x3D,0x00}},
  {'k', {0x7F,0x10,0x28,0x44,0x00}},
  {'l', {0x00,0x41,0x7F,0x40,0x00}},
  {'m', {0x7C,0x04,0x18,0x04,0x78}},
  {'n', {0x7C,0x08,0x04,0x04,0x78}},
  {'o', {0x38,0x44,0x44,0x44,0x38}},
  {'p', {0x7C,0x14,0x14,0x14,0x08}},
  {'q', {0x08,0x14,0x14,0x14,0x7C}},
  {'r', {0x7C,0x08,0x04,0x04,0x08}},
  {'s', {0x48,0x54,0x54,0x54,0x24}},
  {'t', {0x04,0x3F,0x44,0x40,0x20}},
  {'u', {0x3C,0x40,0x40,0x20,0x7C}},
  {'v', {0x1C,0x20,0x40,0x20,0x1C}},
  {'w', {0x3C,0x40,0x30,0x40,0x3C}},
  {'x', {0x44,0x28,0x10,0x28,0x44}},
  {'y', {0x0C,0x50,0x50,0x50,0x3C}},
  {'z', {0x44,0x64,0x54,0x4C,0x44}},

  /* Braces etc. */
  {'{', {0x08,0x36,0x41,0x41,0x00}},
  {'|', {0x00,0x00,0x7F,0x00,0x00}},
  {'}', {0x00,0x41,0x41,0x36,0x08}},
  {'~', {0x08,0x04,0x08,0x10,0x08}},
};

/* Lookup helper */
static int find5x7(char ch){
  /* Direct lookup in sparse table: small linear search is fine (96 max). */
  for (unsigned i=0; i<sizeof(sgfx_font5x7_glyphs)/sizeof(sgfx_font5x7_glyphs[0]); ++i){
    if (sgfx_font5x7_glyphs[i].ch == ch) return (int)i;
  }
  return -1;
}

/* Optional: crop from 8x8 to 5x7 (columns 1..5, rows 0..6) for any missing glyph. */
static void render_5x7_from_8x8(sgfx_device_t* d, int x,int y, char ch, sgfx_rgba8_t c){
#if SGFX_HAVE_FONT8X8
  const uint8_t* g = font8x8_basic[(unsigned char)ch];
  for (int row=0; row<7; ++row){
    uint8_t bits = g[row];                 /* LSB-left row */
    for (int col=0; col<5; ++col){         /* take cols 1..5 for nice spacing */
      if (bits & (uint8_t)(1u << (col+1)))
        sgfx_draw_pixel(d, x+col, y+row, c);
    }
  }
#else
  (void)d; (void)x; (void)y; (void)ch; (void)c;
#endif
}

int sgfx_text5x7_scaled(sgfx_device_t* d, int x, int y,
                        const char* s, sgfx_rgba8_t c,
                        int sx, int sy)
{
  if (!s || sx <= 0 || sy <= 0) return SGFX_ERR_INVAL;

  int cx = x;
  for (; *s; ++s, cx += 6 * sx){
    unsigned char ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) continue;

    int idx = find5x7((char)ch);
    if (idx >= 0){
      const uint8_t* col = sgfx_font5x7_glyphs[idx].col; /* 5 columns */
      for (int i=0;i<5;++i){
        uint8_t cb = col[i];            /* bits 0..6 = rows top..bottom */
        while (cb){
          int row = __builtin_ctz(cb);  /* next set bit */
          /* draw a sx×sy block for this pixel */
          sgfx_fill_rect(d, cx + i*sx, y + row*sy, sx, sy, c);
          cb &= (uint8_t)(cb-1);
        }
      }
    }else{
      /* Fallback: crop 8x8 cols 1..5, rows 0..6 if available */
#if SGFX_HAVE_FONT8X8
      const uint8_t* g = font8x8_basic[ch]; /* row-major, LSB=left */
      for (int row=0; row<7; ++row){
        uint8_t bits = g[row];
        for (int col=0; col<5; ++col){
          if (bits & (uint8_t)(1u << (col+1)))
            sgfx_fill_rect(d, cx + col*sx, y + row*sy, sx, sy, c);
        }
      }
#else
      /* ultimate fallback: outline box */
      sgfx_draw_rect(d, cx, y, 5*sx, 7*sy, c);
#endif
    }
  }
  return SGFX_OK;
}

/* keep old entry point working */
int sgfx_text5x7(sgfx_device_t* d, int x, int y, const char* s, sgfx_rgba8_t c){
  return sgfx_text5x7_scaled(d, x, y, s, c, 1, 1);
}