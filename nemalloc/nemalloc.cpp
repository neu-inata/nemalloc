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

	struct PageHeader {
		uint16_t useCount;
		uint16_t bucketIndex;
		uint16_t padding[2];
	};

	using Offset = uint32_t;
	constexpr Offset END_BUCKET = UINT32_MAX;

	thread_local Offset buckets[NE_SMALL_MEM_ARRAY_SIZE];

	void commit(int bucketIndex) {
		NE_ASSERT(bucketIndex < _countof(buckets));

		// pageIndexの取得
		uint32_t poolIndex = poolHead.fetch_sub(1);
		if (poolIndex == UINT32_MAX) { poolIndex = UINT32_MAX; return; }

		uint32_t pageIndex = pageIndexPool[poolIndex];
		pageIndexPool[poolIndex] = UINT32_MAX;

		// 仮想アドレスのcommit
		uint8_t* page = heap + pageSize * pageIndex;
		[[maybe_unused]] auto res = VirtualAlloc(page, pageSize, MEM_COMMIT, PAGE_READWRITE);
		NE_ASSERT(res == page);

		// ページの先頭に情報を載せる
		PageHeader* header = (PageHeader*)page;
		header->useCount = 0;
		header->bucketIndex = bucketIndex;

		// メモリ内部に位置情報を付ける
		auto& bucketHead = buckets[bucketIndex];
		NE_ASSERT(bucketHead == END_BUCKET);
		bucketHead = (Offset)(page - heap);
		uint8_t* node;
		for (uint32_t i = 1; i < pageSize / (bucketIndex * NE_SMALL_UNIT_SIZE); i++) {
			node = page + (i * bucketIndex * NE_SMALL_UNIT_SIZE);
			*(Offset*)node = (Offset)(node - heap);
		}
		// 最後のnodeには終端であることを示すため
		*(Offset*)node = (Offset)END_BUCKET;

	}

	// メモリ内部の位置情報から該当ページのものを無くす
	void erasePageIndexFromBucket(uint32_t pageIndex) {
		NE_ASSERT(pageIndex * pageSize < reserveSize);
		uint8_t* page = heap + pageSize * pageIndex;
		PageHeader* header = (PageHeader*)page;
		NE_ASSERT(header->useCount == 0);
		auto bucketIndex = header->bucketIndex;

		auto& bucketHead = buckets[bucketIndex];

		auto isPointerInPage = [&](void* p) {
			return (page <= p) && (p < (page + pageSize));
		};

		auto isOffsetInPage = [&](Offset offset) {
			return (pageSize * pageIndex <= offset) && (offset < pageSize * (pageIndex + 1));
		};

		// 先頭が該当pageであれば変更する
		while (isOffsetInPage(bucketHead)) {
			bucketHead = *(Offset*)(heap + bucketHead);
			if (bucketHead == END_BUCKET) { return; }
		}

		uint8_t* node = page + bucketHead;
		while (*node != END_BUCKET) {
			uint8_t* next = page + *(Offset*)node;
			while (isPointerInPage(next)) {
				uint8_t* next = page + *(Offset*)next;
				if (*(Offset*)next == END_BUCKET) {
					*(Offset*)node = END_BUCKET;
					break;
				}
			}
			node = next;
		}
	}

	void decommit(uint32_t pageIndex) {
		NE_ASSERT(pageIndex * pageSize < reserveSize);
		uint8_t* page = heap + pageSize * pageIndex;

		// メモリ内部から該当ページを削除
		erasePageIndexFromBucket(pageIndex);

		// 仮想アドレスのDecommit
		uint8_t* page = heap + pageSize * pageIndex;
		VirtualFree(page, pageSize, MEM_DECOMMIT);

		// pageIndexの返却
		uint32_t poolIndex = poolHead.load();
		NE_ASSERT(pageIndexPool[poolIndex] == UINT32_MAX);
		pageIndexPool[poolIndex] = pageIndex;
		poolHead.fetch_add(1);
		NE_ASSERT(poolHead.load() >= reserveSize / pageSize);

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
