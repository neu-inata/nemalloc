#pragma once
#include <cstdint>

#ifndef NE_SMALL_UNIT_SIZE
#define NE_SMALL_UNIT_SIZE 8
#endif

// SmallHeap:‰Šú’l‚Å‚Í512MB‚ğ—\–ñ
void nemalloc_init(size_t smallHeapReserveSize = 512 * 1024 * 1024);
void* nemalloc(size_t size, uint32_t align_size = NE_SMALL_UNIT_SIZE);
void nefree(void* p);
void nemalloc_finalize();
