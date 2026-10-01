// D:\Work\Fut\cheat\src\log.h
// Crash-safe file logging. printf is invisible in an injected process, and
// OutputDebugStringA is lost on crash -- so every line is appended AND the
// handle closed, guaranteeing the last line before a crash is on disk.
// (Same rationale as the Nemesis logutil / EndFieldHack CtTrace.)
#pragma once
#include <cstdio>   // _snprintf_s

namespace logutil {
void Init(void* moduleHandle);
void TraceLine(const char* line);
}

#define LOG(...) do { char _b[512]; _snprintf_s(_b, sizeof(_b), _TRUNCATE, __VA_ARGS__); logutil::TraceLine(_b); } while (0)
