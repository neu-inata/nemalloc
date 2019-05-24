#include "nemalloc.h"
#include <malloc.h>

void * nemalloc(size_t size, uint32_t align)
{
	return _aligned_malloc(size, align);
}

void nefree(void * p)
{
	_aligned_free(p);
}
