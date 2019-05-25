
#include <iostream>
#include <stdio.h>
#include "nemalloc.h"

int main()
{
	nemalloc_init();
	const size_t strSize = 16;
	char* str = (char*)nemalloc(strSize, 16);

	sprintf_s(str, strSize, "Hello, World");
    std::cout << str << std::endl;

	nefree(str);
	nemalloc_finalize();
}
