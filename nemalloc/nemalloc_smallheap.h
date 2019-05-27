#pragma once
#include <stdint.h>
#include <malloc.h>
#include <atomic>
#include <mutex>
#include <Windows.h>
#include <memoryapi.h>
#include <sysinfoapi.h>

#define NE_SMALL_UNIT_SIZE 8
#define NE_SMALL_SIZE_MAX 256
#define NE_SMALL_MEM_ARRAY_SIZE (NE_SMALL_SIZE_MAX / NE_SMALL_UNIT_SIZE)

// small heap
//[Heap[Page0[[Pool0][Pool1]...][Page1[[Pool0][Pool1]...]...]
namespace ne::sh {

	void Initialize(size_t shReserveSize);
	void Finalize();

	bool Commit(int bucketIndex);
	void Decommit(uint32_t pageIndex);

	void* Allocate(size_t size);
	void Free(const void* ptr);

	bool IsPointerInHeap(const void* const ptr);

}