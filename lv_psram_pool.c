// apps/wilicankit/lv_psram_pool.c — see lv_psram_pool.h.
#include "lv_psram_pool.h"
#include "lv_conf.h"
#include "pico/stdlib.h"   // __uninitialized_psram, via pico/platform/sections.h

void *lv_psram_pool_alloc(size_t size) {
    (void)size;   // always == LV_MEM_SIZE (lv_mem_core_builtin.c's lv_mem_init())
    static uint8_t __uninitialized_psram("lv_mem_pool") pool[LV_MEM_SIZE];
    return pool;
}
