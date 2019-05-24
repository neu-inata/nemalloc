#pragma once
#include <cstdint>

void* nemalloc(size_t size, uint32_t align);
void nefree(void* p);
