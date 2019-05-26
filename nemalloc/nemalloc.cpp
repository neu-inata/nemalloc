#include "nemalloc.h"
#include <malloc.h>
#include <atomic>
#include <mutex>
#include <Windows.h>
#include <memoryapi.h>
#include <sysinfoapi.h>

#define NE_SMALL_SIZE_MAX 256
#define NE_SMALL_MEM_ARRAY_SIZE (NE_SMALL_SIZE_MAX / NE_SMALL_UNIT_SIZE)

static uint32_t pageSize = 0;

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
	static const uint32_t PAGE_INDEX_INVAILED = UINT32_MAX;

	uint32_t poolHead;
	std::mutex commitMutex;

	struct PageHeader {
		uint16_t useCount;
		uint16_t bucketIndex;
		uint16_t padding[2];
	};

	bool commit(int bucketIndex);
	void decommit(uint32_t pageIndex);
	inline int bucketIndex2ElementSize(int bucketIndex);

	struct DecommitMargin {
		uint32_t decommitPool;
		uint64_t availableCount;

		void reserveDecommit(uint32_t pageIndex) {
			if (decommitPool == pageIndex) {
				return;
			}

			uint32_t decommitIndex = decommitPool;
			decommitPool = pageIndex;

			if (decommitIndex != PAGE_INDEX_INVAILED) {
				decommit(decommitIndex);
			}
		}

		inline void addAvailableAndDecommit(int num, int bucketIndex) {
			availableCount += num;
			const uint32_t MARGIN_SIZE = pageSize / bucketIndex2ElementSize(bucketIndex) *  3 / 2 ;
			// ページ使用可能量/2より多くなった場合にdecommitする
			if (availableCount < MARGIN_SIZE) {
				return;
			}

			uint32_t decommitIndex = decommitPool;
			if (decommitIndex != PAGE_INDEX_INVAILED) {
				decommit(decommitIndex);
				decommitPool = PAGE_INDEX_INVAILED;
			}
		}

		inline void subAvailableAndDecommitCancel(int num, uint32_t pageIndex) {
			availableCount -= num;
			uint32_t decommitIndex = decommitPool;

			// 同値なら戻す
			if (decommitIndex == pageIndex) {
				decommitPool = PAGE_INDEX_INVAILED;
			}
		}

		DecommitMargin() {
			decommitPool = PAGE_INDEX_INVAILED;
			availableCount = 0;
		}
	};

	thread_local DecommitMargin decommitMargin[NE_SMALL_MEM_ARRAY_SIZE];

	using Offset = uint32_t;
	constexpr Offset END_BUCKET = 0;

	thread_local Offset buckets[NE_SMALL_MEM_ARRAY_SIZE] = {};

	inline int bucketIndex2ElementSize(int bucketIndex) {
		return (bucketIndex + 1) * NE_SMALL_UNIT_SIZE;
	}

	bool commit(int bucketIndex) {
		NE_ASSERT(bucketIndex < _countof(buckets));

		// pageIndexの取得
		commitMutex.lock();
		uint32_t poolIndex = poolHead--;
		if (poolIndex == UINT32_MAX) {
			poolHead++;
			commitMutex.unlock();
			return false; 
		}

		uint32_t pageIndex = pageIndexPool[poolIndex];
		pageIndexPool[poolIndex] = PAGE_INDEX_INVAILED;
		commitMutex.unlock();

		// 仮想アドレスのcommit
		uint8_t* page = heap + pageSize * pageIndex;
		auto res = VirtualAlloc(page, pageSize, MEM_COMMIT, PAGE_READWRITE);
		NE_ASSERT(res == page);

		// ページの先頭に情報を載せる
		PageHeader* header = (PageHeader*)page;
		header->useCount = 0;
		header->bucketIndex = bucketIndex;

		// メモリ内部に位置情報を付ける
		uint32_t elementSize = bucketIndex2ElementSize(bucketIndex);
		auto& bucketHead = buckets[bucketIndex];
		NE_ASSERT(bucketHead == END_BUCKET);
		bucketHead = (Offset)(page - heap) + elementSize;

		uint8_t* node = heap + bucketHead;
		for (uint32_t i = 0; i < pageSize / elementSize - 2; i++) {
			auto next = node + elementSize;
			*(Offset*)node = (Offset)(next - heap);
			node = next;
		}
		// 最後のnodeには終端であることを示すため
		*(Offset*)node = (Offset)END_BUCKET;

		NE_ASSERT(bucketHead != END_BUCKET);

		// decommit予約用の使用可能数を増やしておく
		//NE_ASSERT(decommitMargin[bucketIndex].availableCount == 0);
		decommitMargin[bucketIndex].availableCount += (pageSize / elementSize) - 1;

		return true;
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
			if (bucketHead == END_BUCKET) { 
				return; 
			}
		}

		uint8_t* node = heap + bucketHead;
		int i = 0;
		while (*(Offset*)node != END_BUCKET) {
			i++;
			uint8_t* next = heap + *(Offset*)node;
			while (isPointerInPage(next)) {
				next = heap + *(Offset*)next;
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
		PageHeader* header = (PageHeader*)page;
		NE_ASSERT(header->useCount == 0);
		uint32_t bucketIndex = header->bucketIndex;
		uint32_t elementSize = bucketIndex2ElementSize(bucketIndex);

		// メモリ内部から該当ページを削除
		erasePageIndexFromBucket(pageIndex);

		// 仮想アドレスのDecommit
		VirtualFree(page, pageSize, MEM_DECOMMIT);

		// pageIndexの返却
		commitMutex.lock();
		uint32_t poolIndex = ++poolHead;
		
		NE_ASSERT(pageIndexPool[poolIndex] == PAGE_INDEX_INVAILED);
		pageIndexPool[poolIndex] = pageIndex;
		commitMutex.unlock();

		// decommit予約用の使用可能数を減らす
		decommitMargin[bucketIndex].availableCount -= (pageSize / elementSize) - 1;

	}

	uint32_t pointer2PageIndex(const void* const ptr) {
		return ((uint8_t*)ptr - heap) / pageSize;;
	}

	void* malloc(size_t size) {
		// ここに呼ばれるときはアライメント済
		NE_ASSERT(size % NE_SMALL_UNIT_SIZE == 0);
		NE_ASSERT(size < NE_SMALL_SIZE_MAX);

		int bucketIndex = (size - 1) / NE_SMALL_UNIT_SIZE;
		auto& bucketHead = buckets[bucketIndex];
		if (bucketHead == END_BUCKET) {
			// commit出来なかったらnullptrを返す
			if (commit(bucketIndex) == false) { return nullptr; }
		}
		bucketHead = buckets[bucketIndex];

		NE_ASSERT(bucketHead != END_BUCKET);

		void* ptr = heap + bucketHead;
		uint32_t pageIndex = pointer2PageIndex(ptr);
		PageHeader* header = (PageHeader*)(heap + pageIndex * pageSize);
		header->useCount++;

		bucketHead = *(Offset*)ptr;

		// decommit予約用の使用可能数を減らす
		NE_ASSERT(decommitMargin[bucketIndex].availableCount != 0);
		decommitMargin[bucketIndex].subAvailableAndDecommitCancel(1, pageIndex);

		return ptr;
	}

	void free(const void* ptr) {
		uint32_t pageIndex = pointer2PageIndex(ptr);
		PageHeader* header = (PageHeader*)(heap + pageIndex * pageSize);
		int bucketIndex = header->bucketIndex;
		auto& bucketHead = buckets[bucketIndex];
		header->useCount--;
		NE_ASSERT(header->useCount < pageSize / bucketIndex2ElementSize(bucketIndex));

		*(Offset*)ptr = bucketHead;
		bucketHead = (uint32_t)((uint8_t*)ptr - heap);

		if (header->useCount == 0) {
			// decommit(pageIndex);
			// ここでdecommitの予約をしておくことで、解放→即取得を防ぐ
			decommitMargin[bucketIndex].reserveDecommit(pageIndex);
		}

		// decommit予約用の使用可能数を増やす
		decommitMargin[bucketIndex].addAvailableAndDecommit(1, bucketIndex);

	}

	bool isPointerInHeap(const void* const ptr) {
		return (heap <= ptr) && (ptr < heap + reserveSize);
	}

};

size_t alignmentSize(size_t size, size_t align)
{
	return (size + align - 1) & ~(align - 1);
}

inline constexpr bool isPowOf2(uint32_t num) {
	return !(num & (num - 1));
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

	sh::pageIndexPool = new uint32_t[shReserveSize / pageSize];
	for (int i = 0; i < shReserveSize / pageSize; i++) {
		sh::pageIndexPool[i] = i;
	}

	sh::poolHead = shReserveSize / pageSize - 1;
}

void * nemalloc(size_t size, uint32_t align)
{
	if (align < NE_SMALL_UNIT_SIZE) { align = NE_SMALL_UNIT_SIZE; }
	NE_ASSERT(isPowOf2(align));
	size = alignmentSize(size, align);

	// 小さなメモリの確保
	if (size <= NE_SMALL_SIZE_MAX) {
		void* ptr = sh::malloc(size);
		if (ptr) { return ptr; }
	}

	return _aligned_malloc(size, align);
}

void nefree(void * p)
{
	if (sh::isPointerInHeap(p)) {
		sh::free(p);
		return;
	}

	_aligned_free(p);
}

void nemalloc_finalize()
{
	static_assert(isPowOf2(NE_SMALL_UNIT_SIZE), "小メモリの最小単位(NE_SMALL_UNIT_SIZE)は2の乗数である必要があります。");
	static_assert(isPowOf2(NE_SMALL_SIZE_MAX),  "小メモリの最大単位(NE_SMALL_SIZE_MAX)は2の乗数である必要があります。");
	static_assert(NE_SMALL_SIZE_MAX % NE_SMALL_UNIT_SIZE == 0, "NE_SMALL_SIZE_MAXはNE_SMALL_UNIT_SIZEの倍数である必要があります。");

	VirtualFree(sh::heap, 0, MEM_RELEASE);
	delete[] sh::pageIndexPool;
}
