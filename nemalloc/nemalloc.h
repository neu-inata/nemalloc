#pragma once
#include <cstdint>

// SmallHeap:初期値では512MBを予約
void nemalloc_init(size_t smallHeapReserveSize = 512 * 1024 * 1024);
void* nemalloc(size_t size, uint32_t align_size = 8);
void nefree(void* p);
void nemalloc_finalize();
