#include "nemalloc_common.h"
#include "nemalloc_smallheap.h"


namespace ne::sh {

	uint32_t reserveSize;
	uint8_t* heap;
	uint32_t* pageIndexPool;
	constexpr uint32_t PAGE_INDEX_INVAILED = UINT32_MAX;

	uint32_t poolHead;
	std::mutex commitMutex;

	using Offset = uint32_t;
	constexpr Offset END_BUCKET = 0;
	thread_local Offset buckets[NE_SMALL_MEM_ARRAY_SIZE] = {};

	struct PageHeader {
		uint16_t useCount;
		uint16_t bucketIndex;
		uint16_t padding[2];
	};

	inline int BucketIndex2ElementSize(int bucketIndex) {
		return (bucketIndex + 1) * NE_SMALL_UNIT_SIZE;
	}

	inline uint32_t Pointer2PageIndex(const void* const ptr) {
		return ((uint8_t*)ptr - heap) / pageSize;;
	}

	bool IsPointerInHeap(const void* const ptr) {
		return (heap <= ptr) && (ptr < heap + reserveSize);
	}

	uint32_t popPageIndex() {
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

		return pageIndex;
	}

	void pushPageIndex(uint32_t pageIndex) {
		commitMutex.lock();
		uint32_t poolIndex = ++poolHead;

		NE_ASSERT(pageIndexPool[poolIndex] == PAGE_INDEX_INVAILED);
		pageIndexPool[poolIndex] = pageIndex;
		commitMutex.unlock();
	}

	struct DecommitMargin {
		uint32_t decommitPool;
		uint64_t availableCount;

		inline void ReserveDecommit(uint32_t pageIndex) {
			if (decommitPool == pageIndex) {
				return;
			}

			uint32_t decommitIndex = decommitPool;
			decommitPool = pageIndex;

			if (decommitIndex != PAGE_INDEX_INVAILED) {
				Decommit(decommitIndex);
			}
		}

		inline void AddAvailableAndDecommit(int num, int bucketIndex) {
			availableCount += num;
			const uint32_t MARGIN_SIZE = pageSize / BucketIndex2ElementSize(bucketIndex) * 3 / 2;
			// �y�[�W�g�p�\��/2��葽���Ȃ����ꍇ��decommit����
			if (availableCount < MARGIN_SIZE) {
				return;
			}

			uint32_t decommitIndex = decommitPool;
			if (decommitIndex != PAGE_INDEX_INVAILED) {
				Decommit(decommitIndex);
				decommitPool = PAGE_INDEX_INVAILED;
			}
		}

		inline void SubAvailableAndDecommitCancel(int num, uint32_t pageIndex) {
			availableCount -= num;
			uint32_t decommitIndex = decommitPool;

			// ���l�Ȃ�߂�
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


	//////////// ���J���� //////////////
	void sh::Initialize(size_t shReserveSize)
	{
		static_assert(IsPowOf2(NE_SMALL_UNIT_SIZE), "���������̍ŏ��P��(NE_SMALL_UNIT_SIZE)��2�̏搔�ł���K�v������܂��B");
		static_assert(IsPowOf2(NE_SMALL_SIZE_MAX), "���������̍ő�P��(NE_SMALL_SIZE_MAX)��2�̏搔�ł���K�v������܂��B");
		static_assert(NE_SMALL_SIZE_MAX % NE_SMALL_UNIT_SIZE == 0, "NE_SMALL_SIZE_MAX��NE_SMALL_UNIT_SIZE�̔{���ł���K�v������܂��B");

		// SmallHeap��4GB�ȉ����w�肵�Ă�������
		NE_ASSERT(shReserveSize < UINT32_MAX);
		sh::reserveSize = shReserveSize;

		SYSTEM_INFO info;
		GetSystemInfo(&info);
		pageSize = info.dwPageSize;

		// Small Heap Initialize
		shReserveSize = AlignmentSize(shReserveSize, info.dwPageSize);
		sh::heap = (uint8_t*)VirtualAlloc(nullptr, shReserveSize, MEM_RESERVE, PAGE_READWRITE);

		sh::pageIndexPool = new uint32_t[shReserveSize / pageSize];
		for (uint32_t i = 0; i < shReserveSize / pageSize; i++) {
			sh::pageIndexPool[i] = i;
		}

		sh::poolHead = shReserveSize / pageSize - 1;
	}

	void Finalize()
	{
		VirtualFree(sh::heap, 0, MEM_RELEASE);
		delete[] sh::pageIndexPool;
	}

	bool Commit(int bucketIndex) {
		NE_ASSERT(bucketIndex < _countof(buckets));

		// pageIndex�̎擾
		uint32_t pageIndex = popPageIndex();

		// ���z�A�h���X��commit
		uint8_t* page = heap + pageSize * pageIndex;
		auto res = VirtualAlloc(page, pageSize, MEM_COMMIT, PAGE_READWRITE);
		NE_ASSERT(res == page);

		// �y�[�W�̐擪�ɏ����ڂ���
		PageHeader* header = (PageHeader*)page;
		header->useCount = 0;
		header->bucketIndex = bucketIndex;

		// �����������Ɉʒu����t����
		uint32_t elementSize = BucketIndex2ElementSize(bucketIndex);
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

		NE_ASSERT(bucketHead != END_BUCKET);

		// decommit�\��p�̎g�p�\���𑝂₵�Ă���
		//NE_ASSERT(decommitMargin[bucketIndex].availableCount == 0);
		decommitMargin[bucketIndex].availableCount += (pageSize / elementSize) - 1;

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

	void Decommit(uint32_t pageIndex) {
		NE_ASSERT(pageIndex * pageSize < reserveSize);
		uint8_t* page = heap + pageSize * pageIndex;
		PageHeader* header = (PageHeader*)page;
		NE_ASSERT(header->useCount == 0);
		uint32_t bucketIndex = header->bucketIndex;
		uint32_t elementSize = BucketIndex2ElementSize(bucketIndex);

		// ��������������Y���y�[�W���폜
		erasePageIndexFromBucket(pageIndex);

		// ���z�A�h���X��Decommit
		VirtualFree(page, pageSize, MEM_DECOMMIT);

		// pageIndex�̕ԋp
		pushPageIndex(pageIndex);

		// decommit�\��p�̎g�p�\�������炷
		decommitMargin[bucketIndex].availableCount -= (pageSize / elementSize) - 1;
	}

	void* Allocate(size_t size) {
		// �����ɌĂ΂��Ƃ��̓A���C�����g��
		NE_ASSERT(size % NE_SMALL_UNIT_SIZE == 0);
		NE_ASSERT(size < NE_SMALL_SIZE_MAX);

		int bucketIndex = (size - 1) / NE_SMALL_UNIT_SIZE;
		auto& bucketHead = buckets[bucketIndex];
		if (bucketHead == END_BUCKET) {
			// commit�o���Ȃ�������nullptr��Ԃ�
			if (Commit(bucketIndex) == false) { return nullptr; }
		}
		bucketHead = buckets[bucketIndex];

		NE_ASSERT(bucketHead != END_BUCKET);

		void* ptr = heap + bucketHead;
		uint32_t pageIndex = Pointer2PageIndex(ptr);
		PageHeader* header = (PageHeader*)(heap + pageIndex * pageSize);
		header->useCount++;

		bucketHead = *(Offset*)ptr;

		// decommit�\��p�̎g�p�\�������炷
		NE_ASSERT(decommitMargin[bucketIndex].availableCount != 0);
		decommitMargin[bucketIndex].SubAvailableAndDecommitCancel(1, pageIndex);

		return ptr;
	}

	void Free(const void* ptr) {
		uint32_t pageIndex = Pointer2PageIndex(ptr);
		PageHeader* header = (PageHeader*)(heap + pageIndex * pageSize);
		int bucketIndex = header->bucketIndex;
		auto& bucketHead = buckets[bucketIndex];
		header->useCount--;
		NE_ASSERT(header->useCount < pageSize / BucketIndex2ElementSize(bucketIndex));

		*(Offset*)ptr = bucketHead;
		bucketHead = (uint32_t)((uint8_t*)ptr - heap);

		if (header->useCount == 0) {
			// decommit(pageIndex);
			// ������decommit�̗\������Ă������ƂŁA��������擾��h��
			decommitMargin[bucketIndex].ReserveDecommit(pageIndex);
		}

		// decommit�\��p�̎g�p�\���𑝂₷
		decommitMargin[bucketIndex].AddAvailableAndDecommit(1, bucketIndex);

	}
}