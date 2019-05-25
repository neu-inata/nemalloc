#include "nemalloc.h"
#include <malloc.h>
#include <atomic>
#include <Windows.h>
#include <memoryapi.h>
#include <sysinfoapi.h>

#define NE_SMALL_UNIT_SIZE 8
#define NE_SMALL_SIZE_MAX 256
#define NE_SMALL_MEM_ARRAY_SIZE (NE_SMALL_SIZE_MAX / NE_SMALL_UNIT_SIZE)

static DWORD pageSize = 0;

#ifdef _DEBUG
#define NE_ASSERT(res) do{ if(res == false){ DebugBreak(); } }while(0)
#else
#define NE_ASSERT(res) 
#endif

// small heap
//[Heap[Page0[[Pool0][Pool1]...][Page1[[Pool0][Pool1]...]...]
namespace sh {
	uint8_t*heap;
	uint32_t* pageIndexPool;
	std::atomic<uint32_t> poolHead;
};

size_t alignmentSize(size_t size, size_t align)
{
	return (size + align - 1) & (~align);
}

void nemalloc_init(size_t shReserveSize)
{
	// SmallHeap‚Í4GBˆÈ‰º‚ðŽw’è‚µ‚Ä‚­‚¾‚³‚¢
	NE_ASSERT(shReserveSize < UINT32_MAX);

	SYSTEM_INFO info;
	GetSystemInfo(&info);
	pageSize = info.dwPageSize;

	// Small Heap Initialize
	shReserveSize = alignmentSize(shReserveSize, info.dwPageSize);
	sh::heap = (uint8_t*)VirtualAlloc(nullptr, shReserveSize, MEM_RESERVE, PAGE_READWRITE);

	sh::pageIndexPool = (uint32_t*)malloc(sizeof(uint32_t) * shReserveSize / pageSize);
	for (int i = 0; i < shReserveSize / pageSize; i++) {
		sh::pageIndexPool[i] = i;
	}
	sh::poolHead = UINT32_MAX;
}

void * nemalloc(size_t size, uint32_t align = NE_SMALL_UNIT_SIZE)
{
	if (align < NE_SMALL_UNIT_SIZE) { align = NE_SMALL_UNIT_SIZE; }
	size = alignmentSize(size, align);

	return _aligned_malloc(size, align);
}

void nefree(void * p)
{
	_aligned_free(p);
}

void nemalloc_finalize()
{
	VirtualFree(sh::heap, 0, MEM_RELEASE);
	free(sh::pageIndexPool);
}
