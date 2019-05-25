#include "nemalloc.h"
#include <malloc.h>
#include <atomic>
#include <Windows.h>
#include <memoryapi.h>
#include <sysinfoapi.h>

#define NE_SMALL_UNIT_SIZE 8
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
	std::atomic<uint32_t> poolHead;

	struct PageHeader {
		uint16_t useCount;
		uint16_t bucketIndex;
		uint16_t padding[2];
	};

	using Offset = uint32_t;
	constexpr Offset END_BUCKET = 0;

	thread_local Offset buckets[NE_SMALL_MEM_ARRAY_SIZE] = {};

	bool commit(int bucketIndex) {
		NE_ASSERT(bucketIndex < _countof(buckets));

		// pageIndex�̎擾
		uint32_t poolIndex = poolHead.fetch_sub(1);
		if (poolIndex == UINT32_MAX) { poolIndex = UINT32_MAX; return false; }

		uint32_t pageIndex = pageIndexPool[poolIndex];
		pageIndexPool[poolIndex] = UINT32_MAX;

		// ���z�A�h���X��commit
		uint8_t* page = heap + pageSize * pageIndex;
		auto res = VirtualAlloc(page, pageSize, MEM_COMMIT, PAGE_READWRITE);
		NE_ASSERT(res == page);

		// �y�[�W�̐擪�ɏ����ڂ���
		PageHeader* header = (PageHeader*)page;
		header->useCount = 0;
		header->bucketIndex = bucketIndex;

		// �����������Ɉʒu����t����
		uint32_t elementSize = bucketIndex * NE_SMALL_UNIT_SIZE;
		auto& bucketHead = buckets[bucketIndex];
		NE_ASSERT(bucketHead == END_BUCKET);
		bucketHead = (Offset)(page - heap) + elementSize;

		uint8_t* node = heap + bucketHead;
		for (uint32_t i = 0; i < pageSize / elementSize - 2; i++) {
			auto next = node + elementSize;
			*(Offset*)node = (Offset)(next - heap);
			node = next;
		}
		// �Ō��node�ɂ͏I�[�ł��邱�Ƃ���������
		*(Offset*)node = (Offset)END_BUCKET;

		return true;
	}

	// �����������̈ʒu��񂩂�Y���y�[�W�̂��̂𖳂���
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

		// �擪���Y��page�ł���ΕύX����
		while (isOffsetInPage(bucketHead)) {
			bucketHead = *(Offset*)(heap + bucketHead);
			if (bucketHead == END_BUCKET) { return; }
		}

		uint8_t* node = heap + bucketHead;
		int i = 0;
		while (*node != END_BUCKET) {
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

		// ��������������Y���y�[�W���폜
		erasePageIndexFromBucket(pageIndex);

		// ���z�A�h���X��Decommit
		VirtualFree(page, pageSize, MEM_DECOMMIT);

		// pageIndex�̕ԋp
		uint32_t poolIndex = poolHead.fetch_add(1);
		// fetch���Ă���1�𑫂��Ȃ��Ɠ��l���擾���鋰�ꂪ����
		poolIndex++;
		NE_ASSERT(pageIndexPool[poolIndex] == UINT32_MAX);
		pageIndexPool[poolIndex] = pageIndex;
		
		NE_ASSERT(poolHead.load() < reserveSize / pageSize);

	}

	uint32_t pointer2PageIndex(const void* const ptr) {
		return ((uint8_t*)ptr - heap) / pageSize;;
	}

	void* malloc(size_t size) {
		// �����ɌĂ΂��Ƃ��̓A���C�����g��
		NE_ASSERT(size % NE_SMALL_UNIT_SIZE == 0);
		NE_ASSERT(size < NE_SMALL_SIZE_MAX);

		int bucketIndex = size / NE_SMALL_UNIT_SIZE;
		auto& bucketHead = buckets[bucketIndex];
		if (bucketHead == END_BUCKET) {
			// commit�o���Ȃ�������nullptr��Ԃ�
			if (commit(bucketIndex) == false) { return nullptr; }
		}
		bucketHead = buckets[bucketIndex];

		NE_ASSERT(bucketHead != END_BUCKET);

		void* ptr = heap + bucketHead;
		uint32_t pageIndex = pointer2PageIndex(ptr);
		PageHeader* header = (PageHeader*)(heap + pageIndex * pageSize);
		header->useCount++;

		bucketHead = *(Offset*)ptr;

		return ptr;
	}

	void free(const void* ptr) {
		uint32_t pageIndex = pointer2PageIndex(ptr);
		PageHeader* header = (PageHeader*)(heap + pageIndex * pageSize);
		int bucketIndex = header->bucketIndex;
		auto& bucketHead = buckets[bucketIndex];
		header->useCount--;
		NE_ASSERT(header->useCount < pageSize / (bucketIndex * NE_SMALL_UNIT_SIZE));

		if (header->useCount == 0) {
			decommit(pageIndex);
		}
		else {
			*(Offset*)ptr = bucketHead;
			bucketHead = (uint32_t)((uint8_t*)ptr - heap);
		}
	}

	bool isPointerInHeap(const void* const ptr) {
		return (heap <= ptr) && (ptr < heap + reserveSize);
	}

};

size_t alignmentSize(size_t size, size_t align)
{
	return (size + align - 1) & ~(align - 1);
}

void nemalloc_init(size_t shReserveSize)
{
	// SmallHeap��4GB�ȉ����w�肵�Ă�������
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

void * nemalloc(size_t size, uint32_t align = NE_SMALL_UNIT_SIZE)
{
	if (align < NE_SMALL_UNIT_SIZE) { align = NE_SMALL_UNIT_SIZE; }
	size = alignmentSize(size, align);

	// �����ȃ������̊m��
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
	VirtualFree(sh::heap, 0, MEM_RELEASE);
	delete[] sh::pageIndexPool;
}
