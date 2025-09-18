/* SGFX Universal Feature Demo
   - Text 5x7 & 8x8 with scaling
   - Clipping, rotation sweep (with restore)
   - Color/mono-friendly fills & gradients
   - Resilient BLIT (tries MONO1 then RGB565)
   - Perf timing
*/

#ifdef ARDUINO
  #include <Arduino.h>
  #define SGFX_DELAY(ms) delay(ms)
  #define SGFX_MILLIS()  millis()
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

extern "C" {
  #include "sgfx.h"
  #include "sgfx_port.h"
}

/* ========= Config / scratch ========= */
#ifndef SGFX_SCRATCH_BYTES
  #define SGFX_SCRATCH_BYTES 4096
#endif
static uint8_t      scratch[SGFX_SCRATCH_BYTES];
static sgfx_device_t dev;

#ifndef SGFX_ROT
  #define SGFX_ROT 0
#endif

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
}

static void scene_addressing(){
  sgfx_clear(&dev, BLACK());
  const int W = dev.caps.width, H = dev.caps.height;
  sgfx_draw_rect(&dev, 0,0, W,H, WHITE());
  sgfx_draw_fast_hline(&dev, 0, H/2, W, WHITE());
  sgfx_draw_fast_vline(&dev, W/2, 0, H, WHITE());
  corner_marks(2, WHITE());
  sgfx_text5x7(&dev, 2, 2, "ADDRESSING", WHITE());
  SGFX_DELAY(800);
}

static void scene_clipping(){
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
}

static void scene_rotation_sweep(uint8_t boot_rot){
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
}

static void scene_text_scaling(){
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
}

static void scene_blit(){
  sgfx_clear(&dev, BLACK());
  sgfx_text5x7(&dev, 2, 2, "BLIT DEMO (auto fmt)", WHITE());

  int rc1 = try_blit_any(8, 16);
  int rc2 = try_blit_any(8+40, 16+6);
  int rc3 = try_blit_any(8+80, 16+12);

  char line[48];
  snprintf(line, sizeof line, "rc=%d,%d,%d", rc1, rc2, rc3);
  sgfx_text5x7(&dev, 2, dev.caps.height-9, line, WHITE());

  SGFX_DELAY(1000);
}

static void scene_color_or_mono_fill(){
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
}

static void scene_perf_fills(){
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
}

static void scene_overlap_merge(){
  sgfx_clear(&dev, BLACK());
  sgfx_text5x7(&dev, 2, 2, "OVERLAP MERGE", WHITE());
  /* Thin lines that share bytes on page-mapped displays (mono) */
  for (int y=10; y<42; y+=3) sgfx_fill_rect(&dev, 8, y, dev.caps.width-16, 1, WHITE());
  for (int x=8; x<dev.caps.width-8; x+=7) sgfx_fill_rect(&dev, x, 9, 1, 36, WHITE());
  SGFX_DELAY(900);
}

/* ========= Arduino-ish entry ========= */

void setup(){
#ifdef ARDUINO
  Serial.begin(115200);
#endif
  if (sgfx_autoinit(&dev, scratch, sizeof scratch)){
#ifdef ARDUINO
    Serial.println("SGFX init failed");
#endif
    while(1){ SGFX_DELAY(1000); }
  }

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

void loop(){}
