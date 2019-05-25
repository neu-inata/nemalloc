
#include <iostream>
#include <stdio.h>
#include <thread>
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

	auto MassSmallMemoryTest = []() {
		printf("大量mallocのテスト - Start\n");
		constexpr uint32_t ARRAY_SIZE = 1024 * 1024 * 8;
		uint8_t** pArray = (uint8_t * *)nemalloc(sizeof(uint8_t*) * ARRAY_SIZE, 8);

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
		printf("大量mallocのテスト - End\n");
	};
	MassSmallMemoryTest();
	
	// スレッドテスト
	{

		printf("大量mallocのテスト - スレッド - Start\n");
		{
			auto threadNum = std::thread::hardware_concurrency();
			std::thread* threads = new std::thread[threadNum];

			for (int i = 0; i < threadNum; i++) {
				threads[i] = std::thread(MassSmallMemoryTest);
			}

			for (int i = 0; i < threadNum; i++) {
				threads[i].join();
			}

			delete[] threads;
		}
		printf("大量mallocのテスト - エンド - End\n");
	}


	nemalloc_finalize();
}
