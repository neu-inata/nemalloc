
#include <iostream>
#include <stdio.h>
#include "nemalloc.h"

int main()
{
	nemalloc_init();

	// Hello, Worldの表示
	{
		const size_t strSize = 16;
		char* str = (char*)nemalloc(strSize, 16);

		sprintf_s(str, strSize, "Hello, World");
		std::cout << str << std::endl;

		nefree(str);
	}

	// 大量mallocのテスト
	{
		constexpr uint32_t ARRAY_SIZE = 1024 * 1024;
		uint8_t** pArray = (uint8_t**)nemalloc(sizeof(uint8_t*) * ARRAY_SIZE, 8);

		for (int i = 0; i < ARRAY_SIZE; i++) {
			auto& p = pArray[i];
			p = (uint8_t*)nemalloc(sizeof(uint8_t), 8);
			*p = rand() % 256;
		}

		for (int i = 0; i < ARRAY_SIZE; i++) {
			auto& p = pArray[i];
			*p = rand() % 256;
			nefree(p);
		}

		nefree(pArray);
	}

	nemalloc_finalize();
}
