#include "nemalloc.h"
#include "nemalloc_common.h"
#include "nemalloc_smallheap.h"

using namespace ne;

void nemalloc_init(size_t shReserveSize)
{
	sh::Initialize(shReserveSize);
}

void * nemalloc(size_t size, uint32_t align)
{
	if (align < NE_SMALL_UNIT_SIZE) { align = NE_SMALL_UNIT_SIZE; }
	size = AlignmentSize(size, align);

	// ¬‚³‚Èƒƒ‚ƒŠ‚ÌŠm•Û
	if (size <= NE_SMALL_SIZE_MAX) {
		void* ptr = sh::Allocate(size);
		if (ptr) { return ptr; }
	}

	return _aligned_malloc(size, align);
}

void nefree(void * p)
{
	if (sh::IsPointerInHeap(p)) {
		sh::Free(p);
		return;
	}

	_aligned_free(p);
}

void nemalloc_finalize()
{
	sh::Finalize();
}
