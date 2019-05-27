#pragma once
#include <cstdint>

#ifdef _DEBUG
#define NE_ASSERT(res) do{ if((res) == false){ DebugBreak(); } }while(0)
#else
#define NE_ASSERT(res) 
#endif

namespace ne {
	inline uint32_t pageSize = 0;


	inline size_t AlignmentSize(size_t size, size_t align)
	{
		return (size + align - 1) & ~(align - 1);
	}

	inline constexpr bool IsPowOf2(uint32_t num) {
		return !(num & (num - 1));
	}
}