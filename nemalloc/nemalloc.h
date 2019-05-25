#pragma once
#include <cstdint>

// SmallHeap:‰Šú’l‚Å‚Í512MB‚ğ—\–ñ
void nemalloc_init(size_t smallHeapReserveSize = 512 * 1024 * 1024);
void* nemalloc(size_t size, uint32_t align_size);
void nefree(void* p);
void nemalloc_finalize();
