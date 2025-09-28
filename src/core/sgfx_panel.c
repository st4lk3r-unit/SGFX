#include "sgfx.h"
void sgfx_set_rotation(sgfx_device_t* d, uint8_t rot){
  if (!d || !d->drv || !d->drv->set_rotation) return;
  d->drv->set_rotation(d, rot);
}
