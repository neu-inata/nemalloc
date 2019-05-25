#pragma once
#include <cstdint>

// SmallHeap:�����l�ł�512MB��\��
void nemalloc_init(size_t smallHeapReserveSize = 512 * 1024 * 1024);
void* nemalloc(size_t size, uint32_t align_size);
void nefree(void* p);
void nemalloc_finalize();
