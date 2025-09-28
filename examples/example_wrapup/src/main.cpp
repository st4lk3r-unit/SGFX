/* SGFX Universal Feature Demo
   - Text 5x7 & 8x8 with scaling
   - Clipping, rotation sweep (with restore)
   - Color/mono-friendly fills & gradients
   - Resilient BLIT (tries MONO1 then RGB565)
   - Perf timing
*/

#include <cstdio>
#ifdef ARDUINO
  #include <Arduino.h>
  #define SGFX_DELAY(ms) delay(ms)
  #define SGFX_MILLIS()  millis()

#include "sgfx.h"
#include "sgfx_fb.h"
#include "sgfx_port.h"

#ifndef SGFX_SCRATCH_BYTES
  #define SGFX_SCRATCH_BYTES 4096
#endif
static uint8_t scratch[SGFX_SCRATCH_BYTES];


/* ---------- A8 overlay & AA text ---------- */
typedef struct { int w,h,stride; uint8_t* a; } a8_fb_t;

static int a8_create(a8_fb_t* o, int w, int h){
  if (w<=0||h<=0) return -1;
  o->w=w; o->h=h; o->stride = w;
  o->a = (uint8_t*)malloc((size_t)w*h);
  if(!o->a) return -1;
  memset(o->a, 0, (size_t)w*h);
  return 0;
}
static void a8_destroy(a8_fb_t* o){ if(o && o->a){ free(o->a); o->a=NULL; } }
static void a8_clear(a8_fb_t* o){ if(o && o->a){ memset(o->a, 0, (size_t)o->w*o->h); } }

/* Blend A8 overlay as solid color onto RGBA fb */
static void a8_blend_to_fb(sgfx_fb_t* fb, a8_fb_t* o, int dx, int dy, sgfx_rgba8_t col){
  if(!fb||!o||!o->a) return;
  int w=o->w, h=o->h;
  for(int j=0;j<h;++j){
    if (dy+j < 0 || dy+j >= fb->h) continue;
    uint8_t* src = o->a + (size_t)j*o->stride;
    sgfx_rgba8_t* dst = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)(dy+j)*fb->stride) + dx;
    for(int i=0;i<w;++i){
      int x = dx + i; if (x<0||x>=fb->w) continue;
      uint8_t a = src[i];
      if(a==0){ /* keep */ }
      else if (a==255){ dst[i] = col; }
      else{
        // non-premultiplied alpha blend in 8-bit
        sgfx_rgba8_t d = dst[i];
        uint8_t ia = (uint8_t)(255 - a);
        dst[i].r = (uint8_t)((d.r*ia + col.r*a + 127)/255);
        dst[i].g = (uint8_t)((d.g*ia + col.g*a + 127)/255);
        dst[i].b = (uint8_t)((d.b*ia + col.b*a + 127)/255);
        dst[i].a = 255;
      }
    }
  }
  sgfx_fb_mark_dirty_px(fb, dx, dy, w, h);
}

/* Measure 5x7 text bbox at scale (sx,sy) */
static inline void measure_5x7(const char* s, int sx,int sy, int* out_w, int* out_h){
  int w = (int)strlen(s) * (5*sx + 1*sx); // 5 cols + 1px spacing scaled
  if (w>0) w -= sx; // no trailing space
  int h = 7*sy;
  if (out_w) *out_w = w;
  if (out_h) *out_h = h;
}

/* Antialiased 5x7 raster into A8 via supersampling (SS×SS) */

static const uint8_t font5x7_digits[11][7] = {
/* '0' */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
/* '1' */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
/* '2' */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
/* '3' */ {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
/* '4' */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
/* '5' */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
/* '6' */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
/* '7' */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
/* '8' */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
/* '9' */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
/* ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static inline const uint8_t* digit_glyph_5x7(char ch){
  if (ch >= '0' && ch <= '9') return font5x7_digits[ch - '0'];
  return font5x7_digits[10]; // space
}

static void a8_text5x7_AA(a8_fb_t* o, int x, int y, const char* s, int sx, int sy, int SS){
  if(!o||!o->a||!s||sx<=0||sy<=0) return;
  int pen_x = x;
  for(const char* p=s; *p; ++p){
    const uint8_t* glyph = digit_glyph_5x7(*p);
    int gw = 5*sx;
    int gh = 7*sy;
    for(int dy=0; dy<gh; ++dy){
      for(int dx=0; dx<gw; ++dx){
        int on = 0;
        for(int syi=0; syi<SS; ++syi){
          for(int sxi=0; sxi<SS; ++sxi){
            float fx = ( (dx + (sxi + 0.5f)/SS) / (float)sx );
            float fy = ( (dy + (syi + 0.5f)/SS) / (float)sy );
            int cx = (int)fx;
            int cy = (int)fy;
            if (cx<0||cx>=5||cy<0||cy>=7) continue;
            uint8_t bits = glyph[cy];
            bool bit_on = (bits & (uint8_t)(1u << (5-1-cx))) != 0;
            on += bit_on;
          }
        }
        uint8_t a = (uint8_t)((on * 255 + (SS*SS/2)) / (SS*SS));
        int px = pen_x + dx;
        int py = y + dy;
        if (px>=0 && px<o->w && py>=0 && py<o->h){
          o->a[(size_t)py*o->stride + px] = a;
        }
      }
    }
    pen_x += gw + sx; // 1px (scaled) spacing
  }
}

/* ---------- FB helpers (scenes use only normalized FB) ---------- */
static inline void fb_mark_dirty_all(sgfx_fb_t* fb){
  sgfx_fb_mark_dirty_px(fb, 0,0, fb->w, fb->h);
}
static inline void fb_mark_dirty_rect(sgfx_fb_t* fb, int x,int y,int w,int h){
  sgfx_fb_mark_dirty_px(fb, x,y,w,h);
}
static inline void fb_full_clear(sgfx_fb_t* fb, sgfx_rgba8_t c){
  if (!fb || !fb->px) return;
  for (int y=0;y<fb->h;++y){
    sgfx_rgba8_t* row = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)y*fb->stride);
    for (int x=0;x<fb->w;++x) row[x] = c;
  }
  fb_mark_dirty_all(fb);
}
static inline void fb_put_px(sgfx_fb_t* fb, int x,int y, sgfx_rgba8_t c){
  if ((unsigned)x >= (unsigned)fb->w || (unsigned)y >= (unsigned)fb->h) return;
  ((sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)y*fb->stride))[x] = c;
  sgfx_fb_mark_dirty_px(fb, x,y,1,1);
}
static inline void fb_draw_fast_hline(sgfx_fb_t* fb, int x,int y,int w, sgfx_rgba8_t c){
  if (y<0 || y>=fb->h) return;
  if (x<0){ w += x; x = 0; }
  if (x+w > fb->w) w = fb->w - x;
  if (w<=0) return;
  sgfx_rgba8_t* row = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)y*fb->stride) + x;
  for (int i=0;i<w;++i) row[i] = c;
  sgfx_fb_mark_dirty_px(fb, x,y,w,1);
}
static inline void fb_draw_fast_vline(sgfx_fb_t* fb, int x,int y,int h, sgfx_rgba8_t c){
  if (x<0 || x>=fb->w) return;
  if (y<0){ h += y; y = 0; }
  if (y+h > fb->h) h = fb->h - y;
  if (h<=0) return;
  for (int j=0;j<h;++j){
    ((sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride))[x] = c;
  }
  sgfx_fb_mark_dirty_px(fb, x,y,1,h);
}
static inline void fb_fill_rect(sgfx_fb_t* fb, int x,int y,int w,int h, sgfx_rgba8_t c){
  if (w<=0||h<=0) return;
  if (x<0){ w += x; x = 0; }
  if (y<0){ h += y; y = 0; }
  if (x+w > fb->w) w = fb->w - x;
  if (y+h > fb->h) h = fb->h - y;
  if (w<=0||h<=0) return;
  for (int j=0;j<h;++j){
    sgfx_rgba8_t* row = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
    for (int i=0;i<w;++i) row[i] = c;
  }
  sgfx_fb_mark_dirty_px(fb, x,y,w,h);
}
static inline void fb_draw_rect(sgfx_fb_t* fb, int x,int y,int w,int h, sgfx_rgba8_t c){
  if (w<=1 || h<=1){ if (w>0 && h>0) fb_put_px(fb, x,y,c); return; }
  fb_draw_fast_hline(fb, x,y, w, c);
  fb_draw_fast_hline(fb, x,y+h-1, w, c);
  fb_draw_fast_vline(fb, x,y, h, c);
  fb_draw_fast_vline(fb, x+w-1,y, h, c);
}


/* 8x8 font (minimal subset for "SGFX " title) rendered into FB */
static const unsigned char font8x8_S[8] = {0x3C,0x42,0x40,0x3C,0x02,0x42,0x3C,0x00};
static const unsigned char font8x8_G[8] = {0x3C,0x42,0x40,0x4E,0x42,0x42,0x3C,0x00};
static const unsigned char font8x8_F[8] = {0x7E,0x40,0x40,0x7C,0x40,0x40,0x40,0x00};
static const unsigned char font8x8_X[8] = {0x42,0x24,0x18,0x18,0x18,0x24,0x42,0x00};
static const unsigned char font8x8_SP[8]= {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

static const unsigned char* glyph8x8_for(unsigned char ch){
  switch(ch){
    case 'S': return font8x8_S;
    case 'G': return font8x8_G;
    case 'F': return font8x8_F;
    case 'X': return font8x8_X;
    case ' ': return font8x8_SP;
    default:  return font8x8_SP;
  }
}
static void fb_text8x8_scaled(sgfx_fb_t* fb, int x,int y, const char* s, sgfx_rgba8_t c, int sx, int sy){
  for (const char* p=s; *p; ++p){
    const unsigned char* glyph = glyph8x8_for((unsigned char)*p);
    for (int row=0; row<8; ++row){
      unsigned char bits = glyph[row];
      for (int col=0; col<8; ++col){
        if (bits & (1u<<col)){
          for (int yy=0; yy<sy; ++yy){
            sgfx_rgba8_t* dst = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)(y + row*sy + yy)*fb->stride) + (x + col*sx);
            for (int xx=0; xx<sx; ++xx) dst[xx] = c;
          }
        }
      }
    }
    x += 8*sx;
  }
  sgfx_fb_mark_dirty_px(fb, x - (int)(strlen(s)*8*sx), y, (int)(strlen(s)*8*sx), 8*sy);
}

/* BLIT helpers into FB (MONO1 and RGB565 sources) */
static inline sgfx_rgba8_t rgba_from_rgb565(uint16_t v){
  uint8_t r = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
  uint8_t g = (uint8_t)(((v >>  5) & 0x3F) * 255 / 63);
  uint8_t b = (uint8_t)(( v        & 0x1F) * 255 / 31);
  return (sgfx_rgba8_t){r,g,b,255};
}
static int fb_blit_mono1(sgfx_fb_t* fb, int x,int y, int W,int H, const uint8_t* mono, size_t stride_bytes){
  for (int j=0;j<H;++j){
    const uint8_t* row = mono + (size_t)j*stride_bytes;
    for (int i=0;i<W;++i){
      uint8_t byte = row[i/8];
      bool on = (byte >> (i&7)) & 1u;
      fb_put_px(fb, x+i, y+j, on ? (sgfx_rgba8_t){255,255,255,255} : (sgfx_rgba8_t){0,0,0,255});
    }
  }
  sgfx_fb_mark_dirty_px(fb, x,y,W,H);
  return SGFX_OK;
}
static int fb_blit_rgb565(sgfx_fb_t* fb, int x,int y, int W,int H, const uint16_t* rgb, size_t stride_bytes){
  for (int j=0;j<H;++j){
    const uint16_t* row = (const uint16_t*)((const uint8_t*)rgb + (size_t)j*stride_bytes);
    for (int i=0;i<W;++i){
      fb_put_px(fb, x+i, y+j, rgba_from_rgb565(row[i]));
    }
  }
  sgfx_fb_mark_dirty_px(fb, x,y,W,H);
  return SGFX_OK;
}

/* Corner marks for FB */
static void corner_marks_fb(sgfx_fb_t* fb, int inset, sgfx_rgba8_t c){
  const int W = fb->w, H = fb->h;
  fb_draw_fast_hline(fb, inset, inset, 8, c);
  fb_draw_fast_vline(fb, inset, inset, 8, c);
  fb_draw_fast_hline(fb, W-8-inset, inset, 8, c);
  fb_draw_fast_vline(fb, W-1-inset, inset, 8, c);
  fb_draw_fast_hline(fb, inset, H-1-inset, 8, c);
  fb_draw_fast_vline(fb, inset, H-8-inset, 8, c);
  fb_draw_fast_hline(fb, W-8-inset, H-1-inset, 8, c);
  fb_draw_fast_vline(fb, W-1-inset, H-8-inset, 8, c);
}

#else
  #include <stdint.h>
  #include <string.h>
  #include <time.h>
  static uint32_t SGFX_MILLIS(){
    static uint64_t t0 = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec*1000ULL + (uint64_t)ts.tv_nsec/1000000ULL;
    if (!t0) t0 = now;
    return (uint32_t)(now - t0);
  }
  static void SGFX_DELAY(uint32_t ms){
    struct timespec ts = { (time_t)(ms/1000), (long)((ms%1000)*1000000L) };
    nanosleep(&ts, NULL);
  }
#endif





static sgfx_device_t dev;
static bool sgfx_ready(){ return (dev.drv != nullptr) && (dev.caps.width>0) && (dev.caps.height>0); }



static void draw_fps_overlay(sgfx_fb_t* fb, float fps){
  int v = (int)(fps + 0.5f);
  if (v < 0) v = 0; if (v > 999) v = 999;
  char buf[16];
  if (v < 100) snprintf(buf, sizeof buf, "%02d", v); else snprintf(buf, sizeof buf, "%03d", v);

  const int sx = 2, sy = 2;   // logical glyph scale
  int tw, th; measure_5x7(buf, sx, sy, &tw, &th);
  const int pad = 3;
  const int bx = 4, by = 4;
  const int ow = tw + pad*2;
  const int oh = th + pad*2;

  a8_fb_t ovl; if (a8_create(&ovl, ow, oh) != 0) return;
  a8_clear(&ovl);
  a8_text5x7_AA(&ovl, pad, pad, buf, sx, sy, 3); // 3x supersampling for smoother edges

  // optional: draw a soft dark backdrop by pre-filling low alpha
  for(int j=0;j<oh;++j){
    uint8_t* row = ovl.a + (size_t)j*ovl.stride;
    for(int i=0;i<ow;++i){
      // keep max(alpha, backdrop_alpha)
      if (row[i] < 40) row[i] = row[i]; // leave as-is; backdrop off (tweak to >=40 for soft box)
    }
  }

  a8_blend_to_fb(fb, &ovl, bx, by, (sgfx_rgba8_t){255,255,255,255});
  a8_destroy(&ovl);
}
/* ========= Color helpers ========= */
static inline sgfx_rgba8_t RGBA(uint8_t r,uint8_t g,uint8_t b,uint8_t a){ return (sgfx_rgba8_t){r,g,b,a}; }
static inline sgfx_rgba8_t WHITE(){ return RGBA(255,255,255,255); }
static inline sgfx_rgba8_t BLACK(){ return RGBA(0,0,0,255); }
static inline sgfx_rgba8_t GRAY(uint8_t v){ return RGBA(v,v,v,255); }
static inline sgfx_rgba8_t RANDCOL(){
  uint8_t r = (uint8_t)(SGFX_MILLIS() * 17u);
  uint8_t g = (uint8_t)(SGFX_MILLIS() * 29u);
  uint8_t b = (uint8_t)(SGFX_MILLIS() * 43u);
  return RGBA(r,g,b,255);
}

/* ========= Small utils ========= */
static inline int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }

static void corner_marks(int inset, sgfx_rgba8_t c){
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_draw_fast_hline(&dev, inset, inset, 8, c);
  sgfx_draw_fast_vline(&dev, inset, inset, 8, c);
  sgfx_draw_fast_hline(&dev, W-8-inset, inset, 8, c);
  sgfx_draw_fast_vline(&dev, W-1-inset, inset, 8, c);
  sgfx_draw_fast_hline(&dev, inset, H-1-inset, 8, c);
  sgfx_draw_fast_vline(&dev, inset, H-8-inset, 8, c);
  sgfx_draw_fast_hline(&dev, W-8-inset, H-1-inset, 8, c);
  sgfx_draw_fast_vline(&dev, W-1-inset, H-8-inset, 8, c);
}

static void checker(int x,int y,int w,int h,int cell){
  for (int j=0;j<h;j+=cell){
    for (int i=0;i<w;i+=cell){
      bool on = ((i/cell) ^ (j/cell)) & 1;
      sgfx_fill_rect(&dev, x+i, y+j, (int)clampi(cell,1,w-i), (int)clampi(cell,1,h-j), on?WHITE():BLACK());
    }
  }
}

/* ========= MONO1 sprite builder (page-oriented, SSD1306 style) ========= */
static void mono1_build_sprite(uint8_t* buf, int W, int H){
  memset(buf, 0x00, (size_t)W*(H/8));
  auto P = [&](int x,int y){
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    int page = y >> 3;
    int bit  = y & 7;
    buf[page*W + x] |= (uint8_t)(1u << bit);
  };
  /* 32x16 "X + box + dots" */
  const int w=32, h=16;
  for (int x=0;x<w;x++){ P(x,0); P(x,h-1); }
  for (int y=0;y<h;y++){ P(0,y); P(w-1,y); }
  for (int i=0;i<((w<h)?w:h);++i){ P(i,i); P(w-1-i,i); }
  for (int y=2;y<h-2;y+=2){
    for (int x=2;x<w-2;x+=4) P(x,y);
  }
}

/* ========= RGB565 sprite builder (checker) ========= */
static uint16_t pack565(sgfx_rgba8_t c){
  return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}
static void rgb565_build_sprite(uint16_t* buf, int W, int H){
  for (int y=0;y<H;++y){
    for (int x=0;x<W;++x){
      bool on = ((x>>2) ^ (y>>2)) & 1;
      sgfx_rgba8_t c = on ? RGBA(255,200,30,255) : RGBA(30,120,255,255);
      buf[y*W + x] = pack565(c);
    }
  }
}

/* ========= BLIT helper that tries MONO1 then RGB565 =========
   Returns 0 on success, otherwise last error. */

/* FB version of try_blit_any */
static int try_blit_any_fb(sgfx_fb_t* fb, int x,int y){
  const int W = 32, H = 16;

#if !defined(SGFX_DEMO_FORCE_RGB565)
  if (SGFX_SCRATCH_BYTES >= (size_t)(W*(H/8))){
    uint8_t* mono = (uint8_t*)scratch;
    mono1_build_sprite(mono, W, H);
    int rc = fb_blit_mono1(fb, x, y, W, H, mono, (size_t)W);
    if (rc == SGFX_OK) return SGFX_OK;
  }
#endif

#if !defined(SGFX_DEMO_FORCE_MONO1)
  if (SGFX_SCRATCH_BYTES >= (size_t)(W*H*2)){
    uint16_t* rgb = (uint16_t*)scratch;
    rgb565_build_sprite(rgb, W, H);
    int rc = fb_blit_rgb565(fb, x, y, W, H, rgb, (size_t)(W*2));
    if (rc == SGFX_OK) return SGFX_OK;
  }
#endif
  return SGFX_ERR_NOSUP;
}
static int try_blit_any(int x,int y){
  const int W = 32, H = 16;

#if defined(SGFX_DEMO_FORCE_MONO1)
  (void)0;
#elif defined(SGFX_DEMO_FORCE_RGB565)
  (void)0;
#endif

  /* Try MONO1 first (fits SSD1306 perfectly) */
#if !defined(SGFX_DEMO_FORCE_RGB565)
  if (SGFX_SCRATCH_BYTES >= (size_t)(W*(H/8))){
    uint8_t* mono = (uint8_t*)scratch;
    mono1_build_sprite(mono, W, H);
    int rc = sgfx_blit(&dev, x, y, W, H, SGFX_FMT_MONO1, mono, (size_t)W);
    if (rc == SGFX_OK) return SGFX_OK;
  }
#endif

  /* Fallback: RGB565 */
#if !defined(SGFX_DEMO_FORCE_MONO1)
  if (SGFX_SCRATCH_BYTES >= (size_t)(W*H*2)){
    uint16_t* rgb = (uint16_t*)scratch;
    rgb565_build_sprite(rgb, W, H);
    int rc = sgfx_blit(&dev, x, y, W, H, SGFX_FMT_RGB565, rgb, (size_t)(W*2));
    if (rc == SGFX_OK) return SGFX_OK;
    return rc;
  }
#endif

  return SGFX_ERR_NOMEM;
}

/* ========= Scenes ========= */

static void scene_intro(uint8_t boot_rot){
  /* --- FB scene wrapper --- */
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t _fb; sgfx_present_t _pr;
  if (sgfx_fb_create(&_fb, W, H, 16, 16) != SGFX_OK || _fb.px == NULL) {
    /* Fallback to device path if FB unavailable */

  sgfx_set_rotation(&dev, boot_rot);
  sgfx_clear(&dev, BLACK());
  const int W = dev.caps.width, H = dev.caps.height;

  /* Big title, auto scaling to fit width */
  int s5 = (W >= 120) ? 2 : 1;
  int s8 = (W >= 200) ? 2 : 1;

  sgfx_text8x8_scaled(&dev, 2, 2, "SGFX", WHITE(), 2*s8, 2*s8);
  sgfx_text5x7_scaled(&dev, 2, 2 + 18*s8, "UNIVERSAL DEMO", WHITE(), s5, s5);
  char line[48];
  snprintf(line, sizeof line, "%dx%d ROT=%u", W, H, (unsigned)boot_rot);
  sgfx_text5x7(&dev, 2, H-9, line, WHITE());

  corner_marks(0, WHITE());
  SGFX_DELAY(1000);

  } else {
    /* FB draw path */

  sgfx_set_rotation(&dev, boot_rot);
  fb_full_clear(&_fb, BLACK());
  const int W = dev.caps.width, H = dev.caps.height;

  /* Big title, auto scaling to fit width */
  int s5 = (W >= 120) ? 2 : 1;
  int s8 = (W >= 200) ? 2 : 1;

  fb_text8x8_scaled(&_fb, 2, 2, "SGFX", WHITE(), 2*s8, 2*s8);
  sgfx_fb_text5x7_scaled(&_fb, 2, 2 + 18*s8, "UNIVERSAL DEMO", WHITE(), s5, s5);
  char line[48];
  snprintf(line, sizeof line, "%dx%d ROT=%u", W, H, (unsigned)boot_rot);
  sgfx_fb_text5x7(&_fb, 2, H-9, line, WHITE());

  corner_marks_fb(&_fb,0, WHITE());
  SGFX_DELAY(1000);

    sgfx_present_init(&_pr, W);
    sgfx_present_frame(&_pr, &dev, &_fb);
    sgfx_present_deinit(&_pr);
    sgfx_fb_destroy(&_fb);
  }
}


static void scene_addressing(){
  /* --- FB scene wrapper --- */
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t _fb; sgfx_present_t _pr;
  if (sgfx_fb_create(&_fb, W, H, 16, 16) != SGFX_OK || _fb.px == NULL) {
    /* Fallback to device path if FB unavailable */

  sgfx_clear(&dev, BLACK());
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_draw_rect(&dev, 0,0, W,H, WHITE());
  sgfx_draw_fast_hline(&dev, 0, H/2, W, WHITE());
  sgfx_draw_fast_vline(&dev, W/2, 0, H, WHITE());
  corner_marks(2, WHITE());
  sgfx_text5x7(&dev, 2, 2, "ADDRESSING", WHITE());
  SGFX_DELAY(800);

  } else {
    /* FB draw path */

  fb_full_clear(&_fb, BLACK());
  const int W = dev.caps.width, H = dev.caps.height;
  fb_draw_rect(&_fb, 0,0, W,H, WHITE());
  fb_draw_fast_hline(&_fb, 0, H/2, W, WHITE());
  fb_draw_fast_vline(&_fb, W/2, 0, H, WHITE());
  corner_marks_fb(&_fb,2, WHITE());
  sgfx_fb_text5x7(&_fb, 2, 2, "ADDRESSING", WHITE());
  SGFX_DELAY(800);

    sgfx_present_init(&_pr, W);
    sgfx_present_frame(&_pr, &dev, &_fb);
    sgfx_present_deinit(&_pr);
    sgfx_fb_destroy(&_fb);
  }
}


static void scene_clipping(){
  /* --- FB scene wrapper --- */
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t _fb; sgfx_present_t _pr;
  if (sgfx_fb_create(&_fb, W, H, 16, 16) != SGFX_OK || _fb.px == NULL) {
    /* Fallback to device path if FB unavailable */

  sgfx_clear(&dev, BLACK());
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_text5x7(&dev, 2, 2, "CLIPPING", WHITE());

  sgfx_rect_t win = { (int16_t)(W/6), (int16_t)(H/6), (int16_t)(2*W/3), (int16_t)(2*H/3) };
  sgfx_set_clip(&dev, win);
  sgfx_draw_rect(&dev, win.x, win.y, win.w, win.h, WHITE());
  checker(win.x+1, win.y+1, win.w-2, win.h-2, 6);
  sgfx_text5x7(&dev, win.x-10, win.y+2, "LEFT CLIP", WHITE());
  sgfx_text5x7(&dev, win.x+win.w-42, win.y+win.h-9, "BTM CLIP", WHITE());
  sgfx_reset_clip(&dev);

  SGFX_DELAY(900);

  } else {
    /* FB draw path */

  fb_full_clear(&_fb, BLACK());
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_text5x7(&_fb, 2, 2, "CLIPPING", WHITE());

  sgfx_rect_t win = { (int16_t)(W/6), (int16_t)(H/6), (int16_t)(2*W/3), (int16_t)(2*H/3) };
  sgfx_set_clip(&dev, win);
  fb_draw_rect(&_fb, win.x, win.y, win.w, win.h, WHITE());
  checker(win.x+1, win.y+1, win.w-2, win.h-2, 6);
  sgfx_fb_text5x7(&_fb, win.x-10, win.y+2, "LEFT CLIP", WHITE());
  sgfx_fb_text5x7(&_fb, win.x+win.w-42, win.y+win.h-9, "BTM CLIP", WHITE());
  sgfx_reset_clip(&dev);

  SGFX_DELAY(900);

    sgfx_present_init(&_pr, W);
    sgfx_present_frame(&_pr, &dev, &_fb);
    sgfx_present_deinit(&_pr);
    sgfx_fb_destroy(&_fb);
  }
}


static void scene_rotation_sweep(uint8_t boot_rot){
  /* --- FB scene wrapper --- */
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t _fb; sgfx_present_t _pr;
  if (sgfx_fb_create(&_fb, W, H, 16, 16) != SGFX_OK || _fb.px == NULL) {
    /* Fallback to device path if FB unavailable */

  const int W = dev.caps.width, H = dev.caps.height;
  for (uint8_t r=0; r<4; ++r){
    sgfx_set_rotation(&dev, r);
    sgfx_clear(&dev, BLACK());
    char buf[32];
    snprintf(buf, sizeof buf, "ROT=%u", (unsigned)r);
    sgfx_text5x7_scaled(&dev, 2, 2, buf, WHITE(), 2, 2);
    corner_marks(0, WHITE());
    sgfx_draw_rect(&dev, 10, 10, W-20, H-20, WHITE());
    SGFX_DELAY(500);
  }
  /* IMPORTANT: restore */
  sgfx_set_rotation(&dev, boot_rot);

  } else {
    /* FB draw path */

  const int W = dev.caps.width, H = dev.caps.height;
  for (uint8_t r=0; r<4; ++r){
    sgfx_set_rotation(&dev, r);
    fb_full_clear(&_fb, BLACK());
    char buf[32];
    snprintf(buf, sizeof buf, "ROT=%u", (unsigned)r);
    sgfx_fb_text5x7_scaled(&_fb, 2, 2, buf, WHITE(), 2, 2);
    corner_marks_fb(&_fb,0, WHITE());
    fb_draw_rect(&_fb, 10, 10, W-20, H-20, WHITE());
    SGFX_DELAY(500);
  }
  /* IMPORTANT: restore */
  sgfx_set_rotation(&dev, boot_rot);

    sgfx_present_init(&_pr, W);
    sgfx_present_frame(&_pr, &dev, &_fb);
    sgfx_present_deinit(&_pr);
    sgfx_fb_destroy(&_fb);
  }
}


static void scene_text_scaling(){
  /* --- FB scene wrapper --- */
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t _fb; sgfx_present_t _pr;
  if (sgfx_fb_create(&_fb, W, H, 16, 16) != SGFX_OK || _fb.px == NULL) {
    /* Fallback to device path if FB unavailable */

  sgfx_clear(&dev, BLACK());
  sgfx_text5x7(&dev, 2, 2, "TEXT SCALING", WHITE());
  int y = 12;
  sgfx_text5x7_scaled(&dev, 2, y,      "5x7 x1", WHITE(), 1,1); y += 10;
  sgfx_text5x7_scaled(&dev, 2, y,      "5x7 x2", WHITE(), 2,2); y += 16;
  sgfx_text5x7_scaled(&dev, 2, y,      "5x7 x3", WHITE(), 3,3); y += 22;
  sgfx_text8x8_scaled(&dev, 2, y,      "8x8 x1", WHITE(), 1,1); y += 12;
  sgfx_text8x8_scaled(&dev, 2, y,      "8x8 x2", WHITE(), 2,2); y += 20;
  sgfx_text8x8_scaled(&dev, 2, y,      "8x8 x2x3", WHITE(), 2,3);
  SGFX_DELAY(1200);

  } else {
    /* FB draw path */

  fb_full_clear(&_fb, BLACK());
  sgfx_fb_text5x7(&_fb, 2, 2, "TEXT SCALING", WHITE());
  int y = 12;
  sgfx_fb_text5x7_scaled(&_fb, 2, y,      "5x7 x1", WHITE(), 1,1); y += 10;
  sgfx_fb_text5x7_scaled(&_fb, 2, y,      "5x7 x2", WHITE(), 2,2); y += 16;
  sgfx_fb_text5x7_scaled(&_fb, 2, y,      "5x7 x3", WHITE(), 3,3); y += 22;
  fb_text8x8_scaled(&_fb, 2, y,      "8x8 x1", WHITE(), 1,1); y += 12;
  fb_text8x8_scaled(&_fb, 2, y,      "8x8 x2", WHITE(), 2,2); y += 20;
  fb_text8x8_scaled(&_fb, 2, y,      "8x8 x2x3", WHITE(), 2,3);
  SGFX_DELAY(1200);

    sgfx_present_init(&_pr, W);
    sgfx_present_frame(&_pr, &dev, &_fb);
    sgfx_present_deinit(&_pr);
    sgfx_fb_destroy(&_fb);
  }
}


static void scene_blit(){
  /* --- FB scene wrapper --- */
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t _fb; sgfx_present_t _pr;
  if (sgfx_fb_create(&_fb, W, H, 16, 16) != SGFX_OK || _fb.px == NULL) {
    /* Fallback to device path if FB unavailable */

  sgfx_clear(&dev, BLACK());
  sgfx_text5x7(&dev, 2, 2, "BLIT DEMO (auto fmt)", WHITE());

  int rc1 = try_blit_any(8, 16);
  int rc2 = try_blit_any(8+40, 16+6);
  int rc3 = try_blit_any(8+80, 16+12);

  char line[48];
  snprintf(line, sizeof line, "rc=%d,%d,%d", rc1, rc2, rc3);
  sgfx_text5x7(&dev, 2, dev.caps.height-9, line, WHITE());

  SGFX_DELAY(1000);

  } else {
    /* FB draw path */

  fb_full_clear(&_fb, BLACK());
  sgfx_fb_text5x7(&_fb, 2, 2, "BLIT DEMO (auto fmt)", WHITE());

  int rc1 = try_blit_any_fb(&_fb, 8, 16);
  int rc2 = try_blit_any_fb(&_fb, 8+40, 16+6);
  int rc3 = try_blit_any_fb(&_fb, 8+80, 16+12);

  char line[48];
  snprintf(line, sizeof line, "rc=%d,%d,%d", rc1, rc2, rc3);
  sgfx_fb_text5x7(&_fb, 2, dev.caps.height-9, line, WHITE());

  SGFX_DELAY(1000);

    sgfx_present_init(&_pr, W);
    sgfx_present_frame(&_pr, &dev, &_fb);
    sgfx_present_deinit(&_pr);
    sgfx_fb_destroy(&_fb);
  }
}


static void scene_color_or_mono_fill(){
  /* --- FB scene wrapper --- */
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t _fb; sgfx_present_t _pr;
  if (sgfx_fb_create(&_fb, W, H, 16, 16) != SGFX_OK || _fb.px == NULL) {
    /* Fallback to device path if FB unavailable */

  sgfx_clear(&dev, BLACK());
  sgfx_text5x7(&dev, 2, 2, "FILLS / GRADIENT", WHITE());
  const int W = dev.caps.width, H = dev.caps.height;

  /* Horizontal gradient (looks nice on color; on mono you'll see dither/palette) */
  for (int x=0; x<W; ++x){
    uint8_t v = (uint8_t)((x * 255) / (W>1?W-1:1));
    sgfx_fill_rect(&dev, x, 12, 1, H/3, RGBA(v, (uint8_t)(255-v), (uint8_t)(v/2), 255));
  }
  /* Vertical bars */
  int y = 12 + H/3 + 2;
  for (int i=0;i<6;++i){
    sgfx_fill_rect(&dev, 2 + i*(W-4)/6, y, (W-8)/6, H - y - 2, RANDCOL());
  }
  SGFX_DELAY(1200);

  } else {
    /* FB draw path */

  fb_full_clear(&_fb, BLACK());
  sgfx_fb_text5x7(&_fb, 2, 2, "FILLS / GRADIENT", WHITE());
  const int W = dev.caps.width, H = dev.caps.height;

  /* Horizontal gradient (looks nice on color; on mono you'll see dither/palette) */
  for (int x=0; x<W; ++x){
    uint8_t v = (uint8_t)((x * 255) / (W>1?W-1:1));
    fb_fill_rect(&_fb, x, 12, 1, H/3, RGBA(v, (uint8_t)(255-v), (uint8_t)(v/2), 255));
  }
  /* Vertical bars */
  int y = 12 + H/3 + 2;
  for (int i=0;i<6;++i){
    fb_fill_rect(&_fb, 2 + i*(W-4)/6, y, (W-8)/6, H - y - 2, RANDCOL());
  }
  SGFX_DELAY(1200);

    sgfx_present_init(&_pr, W);
    sgfx_present_frame(&_pr, &dev, &_fb);
    sgfx_present_deinit(&_pr);
    sgfx_fb_destroy(&_fb);
  }
}


static void scene_perf_fills(){
  /* --- FB scene wrapper --- */
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t _fb; sgfx_present_t _pr;
  if (sgfx_fb_create(&_fb, W, H, 16, 16) != SGFX_OK || _fb.px == NULL) {
    /* Fallback to device path if FB unavailable */

  sgfx_clear(&dev, BLACK());
  sgfx_text5x7(&dev, 2, 2, "PERF: checker fills", WHITE());
  const int frames = 30;
  uint32_t t0 = SGFX_MILLIS();
  for (int f=0; f<frames; ++f){
    checker(0, 10, dev.caps.width, dev.caps.height-10, (f&1)?8:4);
  }
  uint32_t t1 = SGFX_MILLIS();
  float avg_ms = (t1 - t0) / (float)frames;

  char buf[48];
  snprintf(buf, sizeof buf, "AVG=%.1f ms", avg_ms);
  sgfx_clear(&dev, BLACK());
  sgfx_text5x7(&dev, 2, 2, "PERF RESULT", WHITE());
  sgfx_text5x7(&dev, 2, 12, buf, WHITE());
  SGFX_DELAY(900);

  } else {
    /* FB draw path */

  fb_full_clear(&_fb, BLACK());
  sgfx_fb_text5x7(&_fb, 2, 2, "PERF: checker fills", WHITE());
  const int frames = 30;
  uint32_t t0 = SGFX_MILLIS();
  for (int f=0; f<frames; ++f){
    checker(0, 10, dev.caps.width, dev.caps.height-10, (f&1)?8:4);
  }
  uint32_t t1 = SGFX_MILLIS();
  float avg_ms = (t1 - t0) / (float)frames;

  char buf[48];
  snprintf(buf, sizeof buf, "AVG=%.1f ms", avg_ms);
  fb_full_clear(&_fb, BLACK());
  sgfx_fb_text5x7(&_fb, 2, 2, "PERF RESULT", WHITE());
  sgfx_fb_text5x7(&_fb, 2, 12, buf, WHITE());
  SGFX_DELAY(900);

    sgfx_present_init(&_pr, W);
    sgfx_present_frame(&_pr, &dev, &_fb);
    sgfx_present_deinit(&_pr);
    sgfx_fb_destroy(&_fb);
  }
}


static void scene_overlap_merge(){
  /* --- FB scene wrapper --- */
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_fb_t _fb; sgfx_present_t _pr;
  if (sgfx_fb_create(&_fb, W, H, 16, 16) != SGFX_OK || _fb.px == NULL) {
    /* Fallback to device path if FB unavailable */

  sgfx_clear(&dev, BLACK());
  sgfx_text5x7(&dev, 2, 2, "OVERLAP MERGE", WHITE());
  /* Thin lines that share bytes on page-mapped displays (mono) */
  for (int y=10; y<42; y+=3) sgfx_fill_rect(&dev, 8, y, dev.caps.width-16, 1, WHITE());
  for (int x=8; x<dev.caps.width-8; x+=7) sgfx_fill_rect(&dev, x, 9, 1, 36, WHITE());
  SGFX_DELAY(900);

  } else {
    /* FB draw path */

  fb_full_clear(&_fb, BLACK());
  sgfx_fb_text5x7(&_fb, 2, 2, "OVERLAP MERGE", WHITE());
  /* Thin lines that share bytes on page-mapped displays (mono) */
  for (int y=10; y<42; y+=3) fb_fill_rect(&_fb, 8, y, dev.caps.width-16, 1, WHITE());
  for (int x=8; x<dev.caps.width-8; x+=7) fb_fill_rect(&_fb, x, 9, 1, 36, WHITE());
  SGFX_DELAY(900);

    sgfx_present_init(&_pr, W);
    sgfx_present_frame(&_pr, &dev, &_fb);
    sgfx_present_deinit(&_pr);
    sgfx_fb_destroy(&_fb);
  }
}


/* ========= Arduino-ish entry ========= */

void setup(){
  if (sgfx_autoinit(&dev, scratch, sizeof scratch)){
    while(1){ SGFX_DELAY(1000); }
  }

  SGFX_DELAY(1000);

  /* Start at configured rotation (build flag SGFX_ROT if provided) */
  const uint8_t boot_rot = (uint8_t)(SGFX_ROT & 3);
  sgfx_set_rotation(&dev, boot_rot);

  /* Run scenes */
  scene_intro(boot_rot);
  scene_addressing();
  scene_clipping();
  scene_rotation_sweep(boot_rot);   /* auto-restore */
  scene_text_scaling();
  scene_blit();
  scene_color_or_mono_fill();
  scene_perf_fills();
  scene_overlap_merge();

  /* Final */
  sgfx_clear(&dev, BLACK());
  sgfx_text5x7(&dev, 2, 2, "DEMO COMPLETE", WHITE());
  sgfx_text5x7(&dev, 2, 12, "PRESS RESET", WHITE());
}






void loop(){
  static bool g_lazy_init_attempted = false;

  if (!sgfx_ready() && !g_lazy_init_attempted){
    static uint16_t scratch[4096];
    (void)sgfx_autoinit(&dev, scratch, sizeof scratch);
    g_lazy_init_attempted = true;
  }
  if (!sgfx_ready()){ SGFX_DELAY(10); return; }



  // --- Normalized FB animation benchmark (permille + FB composition + HUD overlay) ---
  static bool fb_inited = false;
  static sgfx_fb_t fb_main;       // full screen
  static sgfx_fb_t fb_anim;       // same size (or viewport)
  static sgfx_present_t pr;
  static bool nofb_mode = false;

  struct RectPM { int xpm, ypm, wpm, hpm; int vxpm, vypm; sgfx_rgba8_t c; };
  static RectPM R[5];
  static int W=0,H=0;

  if (!fb_inited){
    W = (int)dev.caps.width;
    H = (int)dev.caps.height;

    int rcA = sgfx_fb_create(&fb_main, W, H, 16, 16);
    int rcB = sgfx_fb_create(&fb_anim, W, H, 16, 16);
    if (rcA != SGFX_OK || rcB != SGFX_OK || fb_main.px == NULL || fb_anim.px == NULL){
      nofb_mode = true;
    }else{
      sgfx_present_init(&pr, W);
      bool fb_supported = dev.drv && dev.drv->set_window && dev.drv->write_pixels;
      if (!fb_supported) { nofb_mode = true; }
    }
    for (int i=0;i<5;++i){
      R[i].wpm = 80 + i*20;  R[i].hpm = 60 + i*15;
      R[i].xpm = (i*150) % 700; R[i].ypm = (i*230) % 700;
      R[i].vxpm = (i&1)? 13 : -11; R[i].vypm = (i&2)? 9 : -7;
      R[i].c = (sgfx_rgba8_t){ (uint8_t)(40+i*40), (uint8_t)(200 - i*30), (uint8_t)(80 + i*30), 255 };
    }
    fb_inited = true;
  }
  if (nofb_mode){
    // Fallback: device drawing
    sgfx_clear(&dev, BLACK());
    for (int i=0;i<5;++i){
      RectPM& r = R[i];
      r.xpm += r.vxpm; r.ypm += r.vypm;
      if (r.xpm < 0 || r.xpm + r.wpm >= 1000){ r.vxpm = -r.vxpm; r.xpm += r.vxpm; }
      if (r.ypm < 0 || r.ypm + r.hpm >= 1000){ r.vypm = -r.vypm; r.ypm += r.vypm; }
      int x = (r.xpm * dev.caps.width  + 500)/1000;
      int y = (r.ypm * dev.caps.height + 500)/1000;
      int w = (r.wpm * dev.caps.width  + 500)/1000;
      int h = (r.hpm * dev.caps.height + 500)/1000;
      sgfx_fill_rect(&dev, x,y,w,h, r.c);
    }
    if (dev.drv && dev.drv->present){ dev.drv->present(&dev); }
    SGFX_DELAY(6);
  } else {
    // Compose on framebuffers

    // Clear anim buffer where rectangles were (simple full clear)
    for (int j=0;j<H;++j){
      memset((uint8_t*)fb_anim.px + (size_t)j*fb_anim.stride, 0x00, (size_t)W*sizeof(sgfx_rgba8_t));
    }
    sgfx_fb_mark_dirty_px(&fb_anim, 0,0,W,H);

    // Move and draw into fb_anim
    for (int i=0;i<5;++i){
      RectPM& r = R[i];
      r.xpm += r.vxpm; r.ypm += r.vypm;
      if (r.xpm < 0 || r.xpm + r.wpm >= 1000){ r.vxpm = -r.vxpm; r.xpm += r.vxpm; }
      if (r.ypm < 0 || r.ypm + r.hpm >= 1000){ r.vypm = -r.vypm; r.ypm += r.vypm; }
      int x = (r.xpm * W + 500)/1000;
      int y = (r.ypm * H + 500)/1000;
      int w = (r.wpm * W + 500)/1000;
      int h = (r.hpm * H + 500)/1000;
      fb_fill_rect(&fb_anim, x,y,w,h, r.c);
    }

    // Copy anim → main (memcpy rows)
    for (int j=0;j<H;++j){
      memcpy((uint8_t*)fb_main.px + (size_t)j*fb_main.stride,
             (const uint8_t*)fb_anim.px + (size_t)j*fb_anim.stride,
             (size_t)W*sizeof(sgfx_rgba8_t));
    }
    sgfx_fb_mark_dirty_px(&fb_main, 0,0,W,H);

    // FPS overlay blended on main
    static float fps_accum = 0.0f;
    static uint32_t t_prev = 0;
    uint32_t t_now = SGFX_MILLIS();
    if (t_prev==0) t_prev = t_now;
    float dt_ms = (float)(t_now - t_prev); t_prev = t_now;
    float fps = (dt_ms > 0.0f) ? (1000.0f / dt_ms) : 0.0f;
    draw_fps_overlay(&fb_main, fps);

    // Present main
    do {
      int _rc = sgfx_present_frame(&pr, &dev, &fb_main);
      if (_rc == SGFX_ERR_NOSUP) { nofb_mode = true; }
    } while(0);
    SGFX_DELAY(6);
  }

}