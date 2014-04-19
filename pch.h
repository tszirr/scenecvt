#define DEBUG_EXCEPTIONS

#include "stdx"
#include "mathx"

#include <string>
#include <vector>

#include <iostream>

#include <functional>
#include <algorithm>

#ifdef WIN32
	#define NOMINMAX
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
#endif

void record_command(const char *tool, const char *file, const char *const *args, size_t argCount);
