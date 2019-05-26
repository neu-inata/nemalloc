
#include <iostream>
#include <stdio.h>
#include <thread>
#include <Windows.h>
#include <sysinfoapi.h>
#include "nemalloc.h"

double getTimeFromStartEnd(const LARGE_INTEGER& start, const LARGE_INTEGER& end) {
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
}

int main()
{
	nemalloc_init();
	LARGE_INTEGER start, end;

	// Hello, Worldの表示
	{
		const size_t strSize = 16;
		char* str = (char*)nemalloc(strSize, 16);

		sprintf_s(str, strSize, "Hello, World");
		std::cout << str << std::endl;

		nefree(str);
	}

#if 0
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
#endif

	// ページ境界でのメモリ確保・解放
	QueryPerformanceCounter(&start);
	printf("ページ境界でのメモリ確保・解放 - Start\n");
	{

		SYSTEM_INFO info;
		GetSystemInfo(&info);

		uint32_t pageSize = info.dwPageSize;
		constexpr uint64_t LOOP_COUNT = 1024 * 1024;
		constexpr int ELEMENT_SIZE = 8;
		uint8_t** pArray = (uint8_t**)nemalloc(sizeof(uint8_t*) * pageSize, 8);

		// 先頭要素はPageHeaderが使用しているため-1
		for (uint32_t i = 0; i < pageSize / ELEMENT_SIZE - 1; i++) {
			pArray[i] = (uint8_t*)nemalloc(ELEMENT_SIZE, 8);
		}

		for (uint64_t i = 0; i < LOOP_COUNT; i++) {
			uint64_t* p = (uint64_t*)nemalloc(ELEMENT_SIZE, 8);
			*p = rand();
			nefree(p);
		}

		for (uint32_t i = 0; i < pageSize / ELEMENT_SIZE - 1; i++) {
			nefree(pArray[i]);
		}

		nefree(pArray);
	};
	QueryPerformanceCounter(&end);
	printf("ページ境界でのメモリ確保・解放 time:%lf[ms] - End\n", getTimeFromStartEnd(start, end));

	nemalloc_finalize();
}
