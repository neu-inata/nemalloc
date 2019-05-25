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
	uint32_t reserveSize;
	uint8_t*heap;
	uint32_t* pageIndexPool;
	std::atomic<uint32_t> poolHead;

	using Offset = uint32_t;

	thread_local Offset buckets[NE_SMALL_MEM_ARRAY_SIZE];

	void commit(int bucketIndex) {
		NE_ASSERT(bucketIndex < _countof(buckets));

		auto& bucket = buckets[bucketIndex];

		// pageIndexの取得
		uint32_t poolIndex = poolHead.fetch_sub(1);
		if (poolIndex == UINT32_MAX) { return; }

		uint32_t pageIndex = pageIndexPool[poolIndex];
		pageIndexPool[poolIndex] = UINT32_MAX;

		// 仮想アドレスのcommit
		uint8_t* page = heap + pageSize * pageIndex;
		[[maybe_unused]] auto res = VirtualAlloc(page, pageSize, MEM_COMMIT, PAGE_READWRITE);
		NE_ASSERT(res == page);

		// メモリ内部に位置情報を付ける
		bucket = (Offset)(page - heap);
		for (uint32_t i = 0; i < pageSize / (bucketIndex * NE_SMALL_UNIT_SIZE); i++) {
			uint8_t* node = page + (i * bucketIndex * NE_SMALL_UNIT_SIZE);
			*(Offset*)node = (Offset)(node - heap);
		}
	}

};

size_t alignmentSize(size_t size, size_t align)
{
	return (size + align - 1) & (~align);
}

void nemalloc_init(size_t shReserveSize)
{
	// SmallHeapは4GB以下を指定してください
	NE_ASSERT(shReserveSize < UINT32_MAX);
	sh::reserveSize = shReserveSize;

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
	sh::poolHead = shReserveSize / pageSize - 1;
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
