// D:\Work\Fut\cheat\src\font.h
#pragma once
struct ImGuiIO;
namespace font {
// Loads the embedded 黑体 (heiti.ttf) with the full CJK glyph range.
bool LoadChineseFont(ImGuiIO& io, float size);
}
