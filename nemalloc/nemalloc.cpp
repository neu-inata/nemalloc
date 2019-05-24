#include "nemalloc.h"
#include <malloc.h>

void * nemalloc(size_t size, uint32_t align)
{
	return malloc(size);
}

void nefree(void * p)
{
	free(p);
}
