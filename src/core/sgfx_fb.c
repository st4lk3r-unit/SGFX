#include "sgfx_fb.h"
#include <stdlib.h>
#include <string.h>

static uint32_t crc32_u8(const uint8_t* p, size_t n){
  uint32_t c=0xFFFFFFFFu;
  for(size_t i=0;i<n;i++){ c ^= p[i];
    for(int k=0;k<8;k++) c = (c>>1) ^ (0xEDB88320u & (-(int)(c&1)));
  }
  return ~c;
}

int sgfx_fb_create(sgfx_fb_t* fb, int w, int h, int tile_w, int tile_h){
  memset(fb,0,sizeof(*fb));
  if (w<=0 || h<=0 || tile_w<=0 || tile_h<=0) return SGFX_ERR_INVAL;
  fb->w=w; fb->h=h; fb->tile_w=tile_w; fb->tile_h=tile_h;
  fb->stride = w * (int)sizeof(sgfx_rgba8_t);
  fb->px = (sgfx_rgba8_t*)calloc((size_t)w*h, sizeof(sgfx_rgba8_t));
  if(!fb->px) return SGFX_ERR_NOMEM;

  fb->tiles_x = (w + tile_w - 1)/tile_w;
  fb->tiles_y = (h + tile_h - 1)/tile_h;
  size_t tiles = (size_t)fb->tiles_x * fb->tiles_y;
  fb->tile_crc   = (uint32_t*)calloc(tiles, sizeof(uint32_t));
  fb->tile_dirty = (uint8_t*) calloc(tiles, 1);
  if(!fb->tile_crc || !fb->tile_dirty){
    free(fb->px); free(fb->tile_crc); free(fb->tile_dirty);
    memset(fb,0,sizeof(*fb));
    return SGFX_ERR_NOMEM; /* partial failure cleaned */
  }
  return SGFX_OK;
}

void sgfx_fb_destroy(sgfx_fb_t* fb){
  free(fb->px); free(fb->tile_crc); free(fb->tile_dirty);
  memset(fb,0,sizeof(*fb));
}

void sgfx_fb_mark_dirty_px(sgfx_fb_t* fb, int x,int y,int w,int h){
  if(w<=0||h<=0) return;
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; }
  if(x+w>fb->w) w = fb->w - x;
  if(y+h>fb->h) h = fb->h - y;
  if(w<=0||h<=0) return;

  int x0 = x / fb->tile_w, x1 = (x+w-1) / fb->tile_w;
  int y0 = y / fb->tile_h, y1 = (y+h-1) / fb->tile_h;
  for(int ty=y0; ty<=y1; ++ty)
    for(int tx=x0; tx<=x1; ++tx)
      fb->tile_dirty[ty*fb->tiles_x + tx] = 1;
}

void sgfx_fb_rehash_tiles(sgfx_fb_t* fb, int x,int y,int w,int h){
  if(w<=0||h<=0) return;
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; }
  if(x+w>fb->w) w = fb->w - x;
  if(y+h>fb->h) h = fb->h - y;
  if(w<=0||h<=0) return;

  int x0 = x / fb->tile_w, x1 = (x+w-1) / fb->tile_w;
  int y0 = y / fb->tile_h, y1 = (y+h-1) / fb->tile_h;
  for(int ty=y0; ty<=y1; ++ty){
    for(int tx=x0; tx<=x1; ++tx){
      int px = tx*fb->tile_w;
      int py = ty*fb->tile_h;
      int tw = (px+fb->tile_w>fb->w)? (fb->w-px): fb->tile_w;
      int th = (py+fb->tile_h>fb->h)? (fb->h-py): fb->tile_h;
      uint8_t* base = (uint8_t*)fb->px + (size_t)py*fb->stride + (size_t)px*sizeof(sgfx_rgba8_t);
      uint32_t crc = 0;
      for(int j=0;j<th;++j)
        crc ^= crc32_u8(base + (size_t)j*fb->stride, (size_t)tw*sizeof(sgfx_rgba8_t));
      size_t idx = (size_t)ty*fb->tiles_x + (size_t)tx;
      if (crc != fb->tile_crc[idx]){ fb->tile_crc[idx]=crc; fb->tile_dirty[idx]=1; }
    }
  }
}

void sgfx_ui_fill_norm(sgfx_fb_t* fb, int xpm,int ypm,int wpm,int hpm, sgfx_rgba8_t c){
  int x = sgfx_pm2px(xpm, fb->w);
  int y = sgfx_pm2px(ypm, fb->h);
  int w = sgfx_pm2px(wpm, fb->w);
  int h = sgfx_pm2px(hpm, fb->h);
  if(w<=0||h<=0) return;
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; }
  if(x+w>fb->w) w = fb->w - x;
  if(y+h>fb->h) h = fb->h - y;
  if(w<=0||h<=0) return;

  for(int j=0;j<h;++j){
    sgfx_rgba8_t* row = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
    for(int i=0;i<w;++i) row[i]=c;
  }
  sgfx_fb_mark_dirty_px(fb, x,y,w,h);
}

/* --- FB Pixel-space helpers (public) --- */
void sgfx_fb_fill_rect_px(sgfx_fb_t* fb, int x, int y, int w, int h, sgfx_rgba8_t c){
  if (!fb || !fb->px) return;
  if (w<=0 || h<=0) return;
  if (x < 0){ w += x; x = 0; }
  if (y < 0){ h += y; y = 0; }
  if (x + w > fb->w) w = fb->w - x;
  if (y + h > fb->h) h = fb->h - y;
  if (w<=0 || h<=0) return;
  for (int j=0; j<h; ++j){
    sgfx_rgba8_t* row = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)(y+j)*fb->stride) + x;
    for (int i=0; i<w; ++i) row[i] = c;
  }
  sgfx_fb_mark_dirty_px(fb, x, y, w, h);
}

/* Draw 5x7 ASCII text using the 8x8 font table cropped to 5x7.
   Scaling: sx,sy >= 1. Only printable 0x20..0x7E. */
extern const uint8_t font8x8_basic[128][8];
void sgfx_fb_text5x7_scaled(sgfx_fb_t* fb, int x, int y, const char* s, sgfx_rgba8_t c, int sx, int sy){
  if (!fb || !fb->px || !s || sx<=0 || sy<=0) return;
  int cx = x;
  int est_w = 0;
  for (const char* p = s; *p; ++p){
    unsigned ch = (unsigned char)*p;
    if (ch >= 32 && ch <= 126) est_w += 6*sx; /* 5px glyph + 1px space */
  }
  if (est_w>0) sgfx_fb_mark_dirty_px(fb, x, y, est_w, 7*sy);

  for (; *s; ++s, cx += 6*sx){
    unsigned ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) continue;
    const uint8_t* glyph = font8x8_basic[ch];
    for (int row=0; row<7; ++row){
      uint8_t bits = glyph[row];
      for (int col=0; col<5; ++col){
        /* crop to columns 1..5 (LSB-left) */
        if (bits & (uint8_t)(1u << (col+1))){
          for (int yy=0; yy<sy; ++yy){
            sgfx_rgba8_t* dst = (sgfx_rgba8_t*)((uint8_t*)fb->px + (size_t)(y + row*sy + yy)*fb->stride) + (cx + col*sx);
            for (int xx=0; xx<sx; ++xx) dst[xx] = c;
          }
        }
      }
    }
  }
}
