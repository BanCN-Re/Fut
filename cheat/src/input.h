// D:\Work\Fut\cheat\src\input.h
// Robust menu-key detection.
//
// Why more than one path: ACE hooks user-mode input APIs, so a plain
// GetAsyncKeyState poll can silently return 0 forever. We therefore run three
// independent detectors and use whichever one actually fires:
//
//   1. the game window's message queue  (WM_KEYDOWN via our WndProc detour)
//   2. RegisterHotKey on our own thread (OS-level, pumped by us)
//   3. GetAsyncKeyState polling         (last resort)
//
// Plus a mouse-clickable fallback button in the on-screen debug strip, so the
// menu can still be opened even if every keyboard path is blocked.
#pragma once
#include <cstdint>

namespace input {

void Init();

// Call from the WndProc detour for every message.
// Returns true when the message was our toggle key and must be swallowed.
bool OnWndProcMessage(void* hwnd, unsigned int msg, uintptr_t wparam, intptr_t lparam);

// Call once per rendered frame (render thread).
void Tick();

// ---- diagnostics (shown in the debug strip and the trace log) ----
const char* ActivePath();
unsigned    CountWndProc();
unsigned    CountHotkey();
unsigned    CountPoll();
bool        KeyDown();

} // namespace input
