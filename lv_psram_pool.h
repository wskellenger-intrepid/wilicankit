// apps/wilicankit/lv_psram_pool.h — backs LVGL's object heap with PSRAM.
// See lv_conf.h's LV_MEM_POOL_INCLUDE/LV_MEM_POOL_ALLOC.
#ifndef LV_PSRAM_POOL_H
#define LV_PSRAM_POOL_H
#include <stddef.h>

// Returns a static PSRAM buffer of at least `size` bytes for lv_mem_init()
// to carve its TLSF pool out of. Called exactly once, with size == LV_MEM_SIZE.
void *lv_psram_pool_alloc(size_t size);

#endif
