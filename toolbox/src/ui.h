#pragma once
#include "common.h"

// 全局 DPI 缩放 + 通用绘制助手(GDI+)。
// S() 把逻辑点(dip)换算为像素,SF() 返回 float 像素,方便布局计算。

// MinGW 4.9.2 头文件没有声明 swprintf_s / sprintf_s,用标准函数替代。
// 调用形式一致:swprintf_s(buf, size, L"fmt", args...) 对应
// swprintf(buf, L"fmt", args...)。宏把多余的 size 参数忽略掉。
#ifndef swprintf_s
#define swprintf_s(buf, bufSize, fmt, ...) swprintf(buf, fmt, ##__VA_ARGS__)
#endif
#ifndef sprintf_s
#define sprintf_s(buf, bufSize, fmt, ...) sprintf(buf, fmt, ##__VA_ARGS__)
#endif
// 代码里还有一处 sprintf_s_safe,这里统一映射到 sprintf_s
#define sprintf_s_safe sprintf_s

extern float g_scale;
extern std::wstring g_uiFont;

int S(int dip);
float SF(float dip);
Gdiplus::Color C(int r, int g, int b, int a = 255);

void InitFontFamily();

// 圆角矩形助手
void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius);
void FillRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius, Gdiplus::Color fill);
void DrawRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius, Gdiplus::Color border, float width);
void FillStrokeRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect, float radius,
                         Gdiplus::Color fill, Gdiplus::Color border, float width);

// 文本
void DrawTextLine(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
                  const Gdiplus::RectF& rect, Gdiplus::Color color,
                  Gdiplus::StringAlignment align = Gdiplus::StringAlignmentNear,
                  bool ellipsis = false);
float MeasureText(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font);

// 颜色工具
Gdiplus::Color ColorFromHex(const std::wstring& hex); // "RRGGBB" or "#RRGGBB"
std::wstring HexFromColor(Gdiplus::Color c);           // "#RRGGBB"
Gdiplus::Color ColorLerp(Gdiplus::Color a, Gdiplus::Color b, float t);

// 十六进制字符串 <-> 字节
std::wstring BytesToHex(const unsigned char* data, size_t len, bool upper);
size_t HexToBytes(const std::wstring& hex, std::vector<unsigned char>& out);

// 宽字符 <-> UTF-8
std::string WideToUtf8Str(const std::wstring& wide);
std::wstring Utf8ToWideStr(const std::string& utf8);

// 简易转义:输出用于 GDI+ 文本框显示的纯文本(不做 markdown)
std::wstring HexDump(const unsigned char* data, size_t len, int bytesPerLine);
