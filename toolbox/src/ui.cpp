#include "ui.h"

using namespace Gdiplus;

float g_scale = 1.0f;
std::wstring g_uiFont = L"Microsoft YaHei UI";

int S(int dip)
{
    int value = (int)((float)dip * g_scale + 0.5f);
    if (dip > 0 && value < 1) value = 1;
    return value;
}

float SF(float dip)
{
    return dip * g_scale;
}

Color C(int r, int g, int b, int a)
{
    return Color((BYTE)a, (BYTE)r, (BYTE)g, (BYTE)b);
}

void InitFontFamily()
{
    FontFamily yaheiUi(L"Microsoft YaHei UI");
    if (yaheiUi.GetLastStatus() == Ok) { g_uiFont = L"Microsoft YaHei UI"; return; }
    FontFamily yahei(L"Microsoft YaHei");
    if (yahei.GetLastStatus() == Ok) { g_uiFont = L"Microsoft YaHei"; return; }
    g_uiFont = L"Segoe UI";
}

void AddRoundRect(GraphicsPath& path, const RectF& rect, float radius)
{
    float maxRadius = (rect.Width < rect.Height ? rect.Width : rect.Height) / 2.0f;
    if (radius > maxRadius) radius = maxRadius;
    if (radius < 0.5f) radius = 0.5f;
    float d = radius * 2.0f;
    float right = rect.X + rect.Width;
    float bottom = rect.Y + rect.Height;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(right - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(right - d, bottom - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, bottom - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRoundRect(Graphics& g, const RectF& rect, float radius, Color fill)
{
    GraphicsPath path;
    AddRoundRect(path, rect, radius);
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
}

void DrawRoundRect(Graphics& g, const RectF& rect, float radius, Color border, float width)
{
    GraphicsPath path;
    AddRoundRect(path, rect, radius);
    Pen pen(border, width);
    g.DrawPath(&pen, &path);
}

void FillStrokeRoundRect(Graphics& g, const RectF& rect, float radius,
                         Color fill, Color border, float width)
{
    GraphicsPath path;
    AddRoundRect(path, rect, radius);
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
    Pen pen(border, width);
    g.DrawPath(&pen, &path);
}

void DrawTextLine(Graphics& g, const std::wstring& text, Font& font,
                  const RectF& rect, Color color, StringAlignment align, bool ellipsis)
{
    if (text.empty()) return;
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    if (ellipsis) format.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush brush(color);
    g.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

float MeasureText(Graphics& g, const std::wstring& text, Font& font)
{
    if (text.empty()) return 0.0f;
    RectF bounds;
    g.MeasureString(text.c_str(), -1, &font, PointF(0, 0), &bounds);
    return bounds.Width;
}

Color ColorFromHex(const std::wstring& hex)
{
    std::wstring s = hex;
    if (!s.empty() && s[0] == L'#') s.erase(0, 1);
    unsigned long value = 0;
    if (s.empty() || _wtol(s.c_str()) == 0 && s.size() < 6)
    {
        // 6-digit
    }
    if (s.size() == 6)
    {
        value = wcstoul(s.c_str(), NULL, 16);
    }
    else if (s.size() == 3)
    {
        int r = (int)_wtol(s.substr(0, 1).c_str()) * 17;
        int g = (int)_wtol(s.substr(1, 1).c_str()) * 17;
        int b = (int)_wtol(s.substr(2, 1).c_str()) * 17;
        return Color(255, r, g, b);
    }
    int r = (int)((value >> 16) & 0xFF);
    int gg = (int)((value >> 8) & 0xFF);
    int b = (int)(value & 0xFF);
    return Color(255, r, gg, b);
}

std::wstring HexFromColor(Color c)
{
    wchar_t buf[8];
    StringCchPrintfW(buf, 8, L"%02X%02X%02X",
                      c.GetR(), c.GetG(), c.GetB());
    return std::wstring(buf);
}

Color ColorLerp(Color a, Color b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int r = (int)(a.GetR() + (b.GetR() - a.GetR()) * t);
    int g = (int)(a.GetG() + (b.GetG() - a.GetG()) * t);
    int bl = (int)(a.GetB() + (b.GetB() - a.GetB()) * t);
    int al = (int)(a.GetA() + (b.GetA() - a.GetA()) * t);
    return Color((BYTE)al, (BYTE)r, (BYTE)g, (BYTE)bl);
}

std::wstring BytesToHex(const unsigned char* data, size_t len, bool upper)
{
    static const wchar_t* kTable = L"0123456789abcdef";
    const wchar_t* table = upper ? L"0123456789ABCDEF" : kTable;
    std::wstring out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        out.push_back(table[data[i] >> 4]);
        out.push_back(table[data[i] & 0xF]);
    }
    return out;
}

std::string WideToUtf8Str(const std::wstring& wide)
{
    if (wide.empty())
    {
        return std::string();
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                     NULL, 0, NULL, NULL);
    if (needed <= 0)
    {
        return std::string();
    }
    std::string out;
    out.resize(needed);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                        &out[0], needed, NULL, NULL);
    return out;
}

std::wstring Utf8ToWideStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return std::wstring();
    }
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                    NULL, 0);
    if (needed <= 0)
    {
        return std::wstring();
    }
    std::wstring out;
    out.resize(needed);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], needed);
    return out;
}

size_t HexToBytes(const std::wstring& hex, std::vector<unsigned char>& out)
{
    out.clear();
    std::wstring s = hex;
    // 去除空格、换行、0x 前缀
    std::wstring clean;
    clean.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        wchar_t c = s[i];
        if (c == L' ' || c == L'\n' || c == L'\r' || c == L'\t' || c == L',') continue;
        clean.push_back(c);
    }
    if (clean.size() >= 2 && clean[0] == L'0' && (clean[1] == L'x' || clean[1] == L'X'))
    {
        clean.erase(0, 2);
    }
    if ((clean.size() & 1) != 0) clean.push_back(L'0');
    out.reserve(clean.size() / 2);
    for (size_t i = 0; i + 1 < clean.size(); i += 2)
    {
        unsigned int hi = (unsigned int)wcstoul(clean.substr(i, 1).c_str(), NULL, 16);
        unsigned int lo = (unsigned int)wcstoul(clean.substr(i + 1, 1).c_str(), NULL, 16);
        out.push_back((unsigned char)((hi << 4) | lo));
    }
    return out.size();
}

std::wstring HexDump(const unsigned char* data, size_t len, int bytesPerLine)
{
    if (bytesPerLine <= 0) bytesPerLine = 16;
    std::wstring out;
    for (size_t i = 0; i < len; i += bytesPerLine)
    {
        size_t lineLen = len - i;
        if (lineLen > bytesPerLine) lineLen = bytesPerLine;
        // 偏移
        wchar_t offBuf[16];
        StringCchPrintfW(offBuf, 16, L"%08X", (unsigned int)(i & 0xFFFFFFFF));
        out += offBuf;
        out += L"  ";
        // 十六进制
        std::wstring hexLine = BytesToHex(data + i, lineLen, false);
        out += hexLine;
        // 对齐空格
        out += L" ";
        // ASCII
        out += L"| ";
        for (size_t k = 0; k < lineLen; ++k)
        {
            unsigned char c = data[i + k];
            out.push_back((c >= 32 && c < 127) ? (wchar_t)c : L'.');
        }
        out += L" |";
        out += L"\n";
    }
    return out;
}
