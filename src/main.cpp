// main.cpp - WinModPlayer: compact Windows 11 tracker module player (libopenmpt)
// UI is fully custom-drawn (GDI+ shapes, GDI ClearType text) to mirror the Android CarModPlayer look.
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <objidl.h>
#include <algorithm>
namespace Gdiplus { using std::max; using std::min; }
#include <gdiplus.h>

#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "audio.h"
#include "playlist.h"
#include "resource.h"

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Constants / colours (same palette as the Android version)
// ---------------------------------------------------------------------------
static const COLORREF C_BG = RGB(0x0F, 0x12, 0x18);
static const COLORREF C_SURFACE = RGB(0x1B, 0x20, 0x2A);
static const COLORREF C_SURFACE2 = RGB(0x26, 0x2D, 0x3B);
static const COLORREF C_INNER = RGB(0x13, 0x17, 0x20);
static const COLORREF C_CYAN = RGB(0x00, 0xE5, 0xFF);
static const COLORREF C_AMBER = RGB(0xFF, 0xB3, 0x00);
static const COLORREF C_TEXT = RGB(0xF0, 0xF4, 0xF8);
static const COLORREF C_TEXT2 = RGB(0x94, 0xA3, 0xB8);
static const COLORREF C_TEXT3 = RGB(0x5B, 0x67, 0x7A);
static const COLORREF C_BORDER = RGB(0x33, 0x41, 0x55);
static const COLORREF C_ERROR = RGB(0xFF, 0x6B, 0x6B);

#define G_PLAY L"\uE768"
#define G_PAUSE L"\uE769"
#define G_PREV L"\uE892"
#define G_NEXT L"\uE893"
#define G_SHUFFLE L"\uE8B1"
#define G_REPEAT1 L"\uE8ED"
#define G_VOLUME L"\uE767"
#define G_MUTE L"\uE74F"
#define G_FOLDER L"\uE838"
#define G_FOLDER_S L"\uE8B7"
#define G_DOC L"\uE8A5"
#define G_LIST L"\uE8FD"
#define G_INSTR L"\uE8D6"
#define G_NOTE L"\uEC4F"
#define G_DRIVE L"\uEDA2"
#define G_EQ L"\uE9E9"
#define G_KEYBOARD L"\uE765"

enum : UINT { WM_APP_TRACKEND = WM_APP + 1, WM_APP_META = WM_APP + 2 };
enum { TIMER_UI = 1 };
enum { THB_PREV = 101, THB_PLAY = 102, THB_NEXT = 103 };

enum Hit {
    H_NONE, H_TAB0, H_TAB1, H_SEEK, H_OPEN, H_SHUFFLE, H_REPEAT, H_PREV, H_PLAY, H_NEXT,
    H_VOLICON, H_VOL, H_LIST, H_SCROLL, H_SHORTCUTS, H_MODAL_CLOSE
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static HINSTANCE g_inst;
static HWND g_hwnd;
static AudioEngine g_audio;
static Library g_lib;
static ModuleInfo g_info;
static bool g_hasInfo = false;
static std::wstring g_curPath, g_curFile, g_curRelDir, g_curExt;
static std::uint64_t g_curSize = 0;
static int g_current = -1, g_selected = -1;
static bool g_shuffle = false, g_repeat = false;
static float g_lastVolume = 0.8f;
static int g_tab = 0;
static std::vector<int> g_history;
static std::mt19937 g_rng{std::random_device{}()};
static std::wstring g_status;
static ULONGLONG g_statusUntil = 0;
static std::wstring g_settingsFile;

struct InfoRow { int kind; int index; std::wstring text; };  // 0 header, 1 text, 2 numbered item
static std::vector<InfoRow> g_infoRows;

// UI
static UINT g_dpi = 96;
static std::wstring g_uiFace = L"Segoe UI", g_iconFace = L"Segoe MDL2 Assets";
static HFONT fBrand, fTab, fTitle, fNormal, fNormalB, fSmall, fSmallB, fTiny, fLabel, fValue, fCode, fMono, fMonoB,
    fIcon14, fIcon16, fIcon20, fIcon24, fIconBig;
static RECT rcHeader, rcTabs, rcTab[2], rcShortcuts, rcCard, rcBadge, rcVu, rcTitle, rcFile, rcFolder, rcInfo1, rcInfo2, rcSeek,
    rcTimes, rcContent, rcContentHdr, rcOpen, rcList, rcControls, rcShuffle, rcRepeat, rcPrev, rcPlay, rcNext,
    rcVolIcon, rcVol, rcModalCard, rcModalClose, rcModalContent;
static bool g_showShortcuts = false;
static int g_shortcutsScroll = 0;

struct ShortcutRow {
    bool isCategory;
    std::wstring key;
    std::wstring desc;
};

static const std::vector<ShortcutRow> g_shortcutsList = {
    { true,  L"PLAYBACK & AUDIO", L"" },
    { false, L"Space",            L"Play / Pause toggle" },
    { false, L"Ctrl + \u2192  /  N",   L"Next track in playlist" },
    { false, L"Ctrl + \u2190  /  P",   L"Previous track (or restart song)" },
    { false, L"\u2190  /  \u2192",          L"Seek backward / forward 5 seconds" },
    { false, L"S",                L"Toggle Random / Shuffle mode" },
    { false, L"R",                L"Toggle Repeat / Continuous Loop" },
    { false, L"+  /  -  /  Wheel",L"Adjust volume up / down" },
    { false, L"M",                L"Mute / Unmute audio" },

    { true,  L"PLAYLIST & BROWSING", L"" },
    { false, L"\u2191  /  \u2193",          L"Select previous / next track" },
    { false, L"Enter",            L"Play selected track immediately" },
    { false, L"PgUp  /  PgDn",    L"Scroll playlist one page" },
    { false, L"Home  /  End",     L"Jump to first / last track" },
    { false, L"Tab",              L"Toggle Playlist / Instruments tab" },
    { false, L"L",                L"Locate & scroll to playing track" },
    { false, L"O",                L"Open folder selection dialog" },
    { false, L"F5",               L"Rescan & refresh current folder" },
    { false, L"Drag & Drop",      L"Drop module files or folder anywhere" },

    { true,  L"APPLICATION", L"" },
    { false, L"F1  /  ?",         L"Toggle this Shortcuts window" },
    { false, L"Esc",              L"Close this Shortcuts window" },
};
static int g_hover = H_NONE, g_hoverRow = -1, g_pressed = H_NONE;
static int g_drag = H_NONE;
static float g_dragFrac = 0;
static int g_dragStartY = 0, g_dragStartScroll = 0;
static int g_scroll[2] = {0, 0};
static float g_vu[64] = {};
static bool g_trackingMouse = false;
static double g_pos = 0;
static HDC g_memDC;
static HBITMAP g_memBmp;
static SIZE g_memSize{};
static Graphics* g_gfx = nullptr;

// taskbar
static UINT g_wmTaskbarCreated;
static ITaskbarList3* g_taskbar = nullptr;
static HICON g_icoPrev, g_icoPlay, g_icoPause, g_icoNext;
static ULONGLONG g_lastTaskbarUpdate = 0;

static int S(double v) { return (int)std::lround(v * g_dpi / 96.0); }
static int W(const RECT& r) { return r.right - r.left; }
static int Hh(const RECT& r) { return r.bottom - r.top; }
static bool In(const RECT& r, POINT p) { return PtInRect(&r, p) != 0; }
static Color GC(COLORREF c, BYTE a = 255) { return Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

// ---------------------------------------------------------------------------
// Format descriptions (from the Android ModuleFormatType)
// ---------------------------------------------------------------------------
struct Fmt { std::wstring code; const wchar_t* name; const wchar_t* sub; COLORREF color; };

static Fmt GetFmt(std::wstring ext) {
    for (auto& c : ext) c = (wchar_t)towlower(c);
    if (ext == L"mod") return {L"MOD", L"Amiga ProTracker", L"Commodore Amiga \u2022 4 Voices", RGB(0xE5, 0x39, 0x35)};
    if (ext == L"xm") return {L"XM", L"FastTracker II", L"Extended Module \u2022 PC DOS", RGB(0x00, 0xE6, 0x76)};
    if (ext == L"it") return {L"IT", L"Impulse Tracker", L"64-Channel Audio Matrix", RGB(0x00, 0xE5, 0xFF)};
    if (ext == L"s3m" || ext == L"stm") return {L"S3M", L"Scream Tracker 3", L"Future Crew \u2022 16 Voices", RGB(0xFF, 0x6D, 0x00)};
    if (ext == L"med" || ext.rfind(L"mmd", 0) == 0) return {L"MED", L"OctaMED", L"Amiga 8-Channel Tracker", RGB(0xAB, 0x47, 0xBC)};
    if (ext == L"mptm") return {L"MPTM", L"OpenMPT Module", L"Modern Tracker Format", RGB(0x42, 0xA5, 0xF5)};
    std::wstring code = ext.empty() ? L"TRACK" : ext.substr(0, 4);
    for (auto& c : code) c = (wchar_t)towupper(c);
    return {code, L"Tracker Module", L"Classic Demoscene Audio", RGB(0x26, 0xA6, 0x9A)};
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
static int CALLBACK EnumFontProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) {
    *(bool*)lp = true;
    return 0;
}
static bool FontExists(const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, face);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, EnumFontProc, (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc);
    return found;
}
static HFONT MkFont(const std::wstring& face, double px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face.c_str());
}
static void DestroyFonts() {
    HFONT* all[] = {&fBrand, &fTab, &fTitle, &fNormal, &fNormalB, &fSmall, &fSmallB, &fTiny, &fLabel, &fValue,
                    &fCode, &fMono, &fMonoB, &fIcon14, &fIcon16, &fIcon20, &fIcon24, &fIconBig};
    for (auto f : all) if (*f) { DeleteObject(*f); *f = nullptr; }
}
static void CreateFonts() {
    DestroyFonts();
    fBrand = MkFont(g_uiFace, 15, FW_BLACK);
    fTab = MkFont(g_uiFace, 11, FW_EXTRABOLD);
    fTitle = MkFont(g_uiFace, 20, FW_BOLD);
    fNormal = MkFont(g_uiFace, 13, FW_NORMAL);
    fNormalB = MkFont(g_uiFace, 13, FW_SEMIBOLD);
    fSmall = MkFont(g_uiFace, 12, FW_NORMAL);
    fSmallB = MkFont(g_uiFace, 12, FW_BOLD);
    fTiny = MkFont(g_uiFace, 11, FW_NORMAL);
    fLabel = MkFont(g_uiFace, 10, FW_BOLD);
    fValue = MkFont(g_uiFace, 18, FW_BLACK);
    fCode = MkFont(g_uiFace, 13, FW_BLACK);
    fMono = MkFont(L"Consolas", 12.5, FW_NORMAL);
    fMonoB = MkFont(L"Consolas", 12.5, FW_BOLD);
    fIcon14 = MkFont(g_iconFace, 14, FW_NORMAL);
    fIcon16 = MkFont(g_iconFace, 16, FW_NORMAL);
    fIcon20 = MkFont(g_iconFace, 18, FW_NORMAL);
    fIcon24 = MkFont(g_iconFace, 22, FW_NORMAL);
    fIconBig = MkFont(g_iconFace, 40, FW_NORMAL);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void RoundPath(GraphicsPath& p, REAL x, REAL y, REAL w, REAL h, REAL r) {
    r = std::min(r, std::min(w, h) / 2);
    REAL d = r * 2;
    if (d <= 0.5f) { p.AddRectangle(RectF(x, y, w, h)); return; }
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}
static void FillRR(const RECT& r, double rad, Color c) {
    GraphicsPath p;
    RoundPath(p, (REAL)r.left, (REAL)r.top, (REAL)W(r), (REAL)Hh(r), (REAL)S(rad));
    SolidBrush b(c);
    g_gfx->FillPath(&b, &p);
}
static void StrokeRR(const RECT& r, double rad, Color c, double width = 1.5) {
    REAL w = (REAL)std::max(1.0, width * g_dpi / 96.0), h2 = w / 2;
    GraphicsPath p;
    RoundPath(p, r.left + h2, r.top + h2, W(r) - w, Hh(r) - w, (REAL)S(rad));
    Pen pen(c, w);
    g_gfx->DrawPath(&pen, &p);
}
static void FillCircle(const RECT& r, Color c) {
    SolidBrush b(c);
    g_gfx->FillEllipse(&b, (REAL)r.left, (REAL)r.top, (REAL)W(r), (REAL)Hh(r));
}
static void Txt(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c,
                UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX) {
    g_gfx->Flush(FlushIntentionSync);
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt);
}
static void Glyph(HDC dc, const wchar_t* g, const RECT& r, HFONT f, COLORREF c) {
    Txt(dc, g, r, f, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
static int TextWidth(HDC dc, const std::wstring& s, HFONT f) {
    SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    return sz.cx;
}
static RECT CenterRect(int cx, int cy, int w, int h) { return {cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h}; }

// ---------------------------------------------------------------------------
// Settings (INI in %APPDATA%\WinModPlayer)
// ---------------------------------------------------------------------------
static void InitSettingsPath() {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        std::wstring dir = std::wstring(p) + L"\\WinModPlayer";
        CoTaskMemFree(p);
        CreateDirectoryW(dir.c_str(), nullptr);
        g_settingsFile = dir + L"\\settings.ini";
    }
}
static std::wstring GetSetting(const wchar_t* key, const wchar_t* def = L"") {
    wchar_t buf[4096];
    GetPrivateProfileStringW(L"Player", key, def, buf, 4096, g_settingsFile.c_str());
    return buf;
}
static int GetSettingInt(const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(L"Player", key, def, g_settingsFile.c_str());
}
static void PutSetting(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(L"Player", key, v.c_str(), g_settingsFile.c_str());
}
static void PutSettingInt(const wchar_t* key, int v) { PutSetting(key, std::to_wstring(v)); }

static void SaveState() {
    PutSetting(L"Folder", g_lib.Root());
    PutSetting(L"Track", g_curPath);
    PutSettingInt(L"Shuffle", g_shuffle);
    PutSettingInt(L"Repeat", g_repeat);
    PutSettingInt(L"Volume", (int)std::lround(g_audio.Volume() * 100));
    PutSettingInt(L"Tab", g_tab);
}
static void SaveWindowPlacement() {
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (!GetWindowPlacement(g_hwnd, &wp)) return;
    RECT r = wp.rcNormalPosition;
    PutSettingInt(L"X", r.left);
    PutSettingInt(L"Y", r.top);
    PutSettingInt(L"W", r.right - r.left);
    PutSettingInt(L"H", r.bottom - r.top);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static int RowH() { return g_tab == 0 ? S(48) : S(21); }
static int ContentH() { return RowH() * (int)(g_tab == 0 ? g_lib.tracks.size() : g_infoRows.size()); }
static int MaxScroll() { return std::max(0, ContentH() - Hh(rcList)); }
static void ClampScroll() { g_scroll[g_tab] = std::clamp(g_scroll[g_tab], 0, MaxScroll()); }

static void Layout() {
    RECT c;
    GetClientRect(g_hwnd, &c);
    int w = c.right, h = c.bottom, P = S(14);

    rcHeader = {P, S(10), w - P, S(52)};
    int tabW = S(108), tabH = S(32), cy = (rcHeader.top + rcHeader.bottom) / 2;
    rcTabs = {w - P - 2 * tabW - S(10), cy - tabH / 2 - S(2), w - P, cy + tabH / 2 + S(2)};
    rcTab[0] = {rcTabs.left + S(4), rcTabs.top + S(4), rcTabs.left + S(4) + tabW, rcTabs.bottom - S(4)};
    rcTab[1] = {rcTab[0].right + S(2), rcTab[0].top, rcTabs.right - S(4), rcTab[0].bottom};

    int scH = S(32);
    bool wideSc = (w >= S(520));
    int scW = wideSc ? S(114) : S(36);
    rcShortcuts = {rcTabs.left - scW - S(8), cy - scH / 2, rcTabs.left - S(8), cy + scH / 2};

    int y = rcHeader.bottom + S(6);
    rcCard = {P, y, w - P, 0};
    int x0 = P + S(16), x1 = w - P - S(16);
    y += S(14);
    rcVu = {x1 - S(124), y + S(4), x1, y + S(36)};
    rcBadge = {x0, y, rcVu.left - S(10), y + S(40)};
    y += S(40) + S(10);
    rcTitle = {x0, y, x1, y + S(30)}; y += S(30);
    rcFile = {x0, y, x1, y + S(20)}; y += S(20);
    rcFolder = {x0, y, x1, y + S(20)}; y += S(20) + S(10);
    int bw = (x1 - x0 - S(12)) / 2;
    rcInfo1 = {x0, y, x0 + bw, y + S(56)};
    rcInfo2 = {x1 - bw, y, x1, y + S(56)};
    y += S(56) + S(10);
    rcSeek = {x0, y, x1, y + S(22)}; y += S(22);
    rcTimes = {x0, y, x1, y + S(16)}; y += S(16) + S(12);
    rcCard.bottom = y;

    int ch = S(80);
    rcControls = {0, h - ch, w, h};
    int ccy = h - ch / 2, cx = w / 2;
    rcPlay = CenterRect(cx, ccy, S(58), S(58));
    rcPrev = CenterRect(cx - S(29) - S(14) - S(23), ccy, S(46), S(46));
    rcNext = CenterRect(cx + S(29) + S(14) + S(23), ccy, S(46), S(46));
    rcShuffle = {P, ccy - S(20), P + S(40), ccy + S(20)};
    rcRepeat = {P + S(46), ccy - S(20), P + S(86), ccy + S(20)};
    int volW = std::min(S(96), std::max(S(40), (w / 2 - S(29) - S(14) - S(46)) - P - S(48) - S(10)));
    rcVol = {w - P - volW, ccy - S(11), w - P, ccy + S(11)};
    rcVolIcon = {rcVol.left - S(36), ccy - S(18), rcVol.left - S(4), ccy + S(18)};

    rcContent = {P, rcCard.bottom + S(10), w - P, std::max((int)rcCard.bottom + S(60), (int)rcControls.top - S(10))};
    rcContentHdr = {rcContent.left + S(14), rcContent.top + S(8), rcContent.right - S(12), rcContent.top + S(44)};
    rcOpen = {rcContentHdr.right - S(124), rcContentHdr.top + S(4), rcContentHdr.right, rcContentHdr.bottom - S(4)};
    rcList = {rcContent.left + S(6), rcContentHdr.bottom + S(4), rcContent.right - S(6), rcContent.bottom - S(8)};
    ClampScroll();
}

static RECT ScrollTrack() { return {rcList.right - S(8), rcList.top + S(2), rcList.right - S(2), rcList.bottom - S(2)}; }
static bool ScrollThumb(RECT& out) {
    int ch = ContentH(), lh = Hh(rcList);
    if (ch <= lh || lh <= 0) return false;
    RECT t = ScrollTrack();
    int th = std::max(S(28), Hh(t) * lh / ch);
    int ty = t.top + (int)((long long)(Hh(t) - th) * g_scroll[g_tab] / std::max(1, ch - lh));
    out = {t.left, ty, t.right, ty + th};
    return true;
}

static void EnsureVisible(int idx) {
    if (g_tab != 0 || idx < 0) return;
    int rh = S(48), top = idx * rh;
    if (top < g_scroll[0]) g_scroll[0] = top;
    else if (top + rh > g_scroll[0] + Hh(rcList)) g_scroll[0] = top + rh - Hh(rcList);
    ClampScroll();
}

// ---------------------------------------------------------------------------
// Taskbar integration (thumbnail buttons + progress)
// ---------------------------------------------------------------------------
static HICON MakeGlyphIcon(const wchar_t* glyph, int size, COLORREF color) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, dib);
    RECT r{0, 0, size, size};
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    HFONT f = CreateFontW(-(size * 3 / 4), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0,
                          g_iconFace.c_str());
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, glyph, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    DWORD* px = (DWORD*)bits;
    for (int i = 0; i < size * size; ++i) {
        DWORD v = px[i];
        BYTE a = (BYTE)std::max({(v >> 16) & 255, (v >> 8) & 255, v & 255});
        px[i] = ((DWORD)a << 24) | ((DWORD)GetRValue(color) << 16) | ((DWORD)GetGValue(color) << 8) | GetBValue(color);
    }
    SelectObject(dc, old);
    DeleteObject(f);
    DeleteDC(dc);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, dib};
    HICON ico = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(dib);
    return ico;
}

static bool IsPlaying() { return g_audio.HasModule() && !g_audio.IsPaused(); }

static void UpdateThumbButtons() {
    if (!g_taskbar) return;
    THUMBBUTTON b{};
    b.dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
    b.iId = THB_PLAY;
    b.hIcon = IsPlaying() ? g_icoPause : g_icoPlay;
    wcscpy_s(b.szTip, IsPlaying() ? L"Pause" : L"Play");
    b.dwFlags = THBF_ENABLED;
    g_taskbar->ThumbBarUpdateButtons(g_hwnd, 1, &b);
}

static void UpdateTaskbarProgress() {
    if (!g_taskbar) return;
    if (!g_audio.HasModule() || g_info.duration <= 0) {
        g_taskbar->SetProgressState(g_hwnd, TBPF_NOPROGRESS);
        return;
    }
    g_taskbar->SetProgressState(g_hwnd, g_audio.IsPaused() ? TBPF_PAUSED : TBPF_NORMAL);
    ULONGLONG total = (ULONGLONG)(g_info.duration * 1000);
    ULONGLONG cur = (ULONGLONG)std::clamp(g_pos * 1000, 0.0, (double)total);
    g_taskbar->SetProgressValue(g_hwnd, cur, total);
}

static void InitTaskbar() {
    if (g_taskbar) { g_taskbar->Release(); g_taskbar = nullptr; }
    if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_taskbar)))) return;
    if (FAILED(g_taskbar->HrInit())) { g_taskbar->Release(); g_taskbar = nullptr; return; }
    int sz = GetSystemMetricsForDpi(SM_CXSMICON, g_dpi);
    if (!g_icoPrev) {
        g_icoPrev = MakeGlyphIcon(G_PREV, sz, RGB(255, 255, 255));
        g_icoPlay = MakeGlyphIcon(G_PLAY, sz, RGB(255, 255, 255));
        g_icoPause = MakeGlyphIcon(G_PAUSE, sz, RGB(255, 255, 255));
        g_icoNext = MakeGlyphIcon(G_NEXT, sz, RGB(255, 255, 255));
    }
    THUMBBUTTON b[3]{};
    const wchar_t* tips[3] = {L"Previous", L"Play", L"Next"};
    HICON icons[3] = {g_icoPrev, g_icoPlay, g_icoNext};
    UINT ids[3] = {THB_PREV, THB_PLAY, THB_NEXT};
    for (int i = 0; i < 3; ++i) {
        b[i].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        b[i].iId = ids[i];
        b[i].hIcon = icons[i];
        b[i].dwFlags = THBF_ENABLED;
        wcscpy_s(b[i].szTip, tips[i]);
    }
    g_taskbar->ThumbBarAddButtons(g_hwnd, 3, b);
    UpdateThumbButtons();
}

// ---------------------------------------------------------------------------
// Player logic
// ---------------------------------------------------------------------------
static void Invalidate() { InvalidateRect(g_hwnd, nullptr, FALSE); }

static void SetStatus(const std::wstring& s) {
    g_status = s;
    g_statusUntil = GetTickCount64() + 5000;
    Invalidate();
}

static void UpdateTitle() {
    std::wstring t = L"WinModPlayer";
    if (g_hasInfo) {
        std::wstring name = g_info.title.empty() ? g_curFile : g_info.title;
        t = name + L" \u2014 WinModPlayer";
    }
    SetWindowTextW(g_hwnd, t.c_str());
}

static void BuildInfoRows() {
    g_infoRows.clear();
    if (!g_hasInfo) return;
    g_infoRows.push_back({0, 0, L"MODULE INFO"});
    if (!g_info.typeLong.empty()) g_infoRows.push_back({1, 0, L"Format    : " + g_info.typeLong});
    if (!g_info.tracker.empty()) g_infoRows.push_back({1, 0, L"Tracker   : " + g_info.tracker});
    if (!g_info.artist.empty()) g_infoRows.push_back({1, 0, L"Artist    : " + g_info.artist});
    g_infoRows.push_back({1, 0, L"Channels  : " + std::to_wstring(g_info.channels) + L"    Patterns: " +
                                    std::to_wstring(g_info.patterns) + L"    Orders: " + std::to_wstring(g_info.orders)});
    if (!g_info.message.empty()) {
        g_infoRows.push_back({0, 0, L""});
        g_infoRows.push_back({0, 0, L"SONG MESSAGE"});
        std::wstring line;
        for (size_t i = 0; i <= g_info.message.size(); ++i) {
            wchar_t ch = i < g_info.message.size() ? g_info.message[i] : L'\n';
            if (ch == L'\r') {
                if (i + 1 < g_info.message.size() && g_info.message[i + 1] == L'\n') ++i;
                ch = L'\n';
            }
            if (ch == L'\n') { g_infoRows.push_back({1, 0, line}); line.clear(); }
            else line += ch;
        }
        while (!g_infoRows.empty() && g_infoRows.back().kind == 1 && g_infoRows.back().text.empty()) g_infoRows.pop_back();
    }
    auto addList = [](const wchar_t* title, const std::vector<std::wstring>& v) {
        if (v.empty()) return;
        g_infoRows.push_back({0, 0, L""});
        g_infoRows.push_back({0, 0, std::wstring(title) + L" (" + std::to_wstring(v.size()) + L")"});
        for (size_t i = 0; i < v.size(); ++i) g_infoRows.push_back({2, (int)i + 1, v[i]});
    };
    addList(L"INSTRUMENTS", g_info.instruments);
    addList(L"SAMPLES", g_info.samples);
}

static bool PlayIndex(int idx, bool pushHistory = true, bool autoplay = true) {
    if (idx < 0 || idx >= (int)g_lib.tracks.size()) return false;
    Track& t = *g_lib.tracks[idx];
    ModuleInfo info;
    std::wstring err;
    g_audio.SetPaused(true);
    if (!g_audio.Load(t.path, info, err)) {
        SetStatus(t.fileName + L": " + err);
        return false;
    }
    g_info = std::move(info);
    g_hasInfo = true;
    g_current = g_selected = idx;
    g_curPath = t.path;
    g_curFile = t.fileName;
    g_curRelDir = t.relDir;
    g_curExt = t.ext;
    g_curSize = t.size;
    g_pos = 0;
    g_status.clear();
    BuildInfoRows();
    g_scroll[1] = 0;
    if (pushHistory) {
        g_history.push_back(idx);
        if (g_history.size() > 1000) g_history.erase(g_history.begin());
    }
    g_audio.SetPaused(!autoplay);
    EnsureVisible(idx);
    UpdateTitle();
    SaveState();
    UpdateThumbButtons();
    UpdateTaskbarProgress();
    Invalidate();
    return true;
}

static int RandomOther(int cur) {
    int n = (int)g_lib.tracks.size();
    if (n <= 1) return 0;
    std::uniform_int_distribution<int> d(0, n - 1);
    int i;
    do { i = d(g_rng); } while (i == cur);
    return i;
}

static void NextTrack() {
    int n = (int)g_lib.tracks.size();
    if (!n) return;
    int base = g_current;
    for (int a = 0; a < std::min(n, 50); ++a) {
        int idx = g_shuffle ? RandomOther(base) : (base < 0 ? 0 : (base + 1) % n);
        if (PlayIndex(idx)) return;
        base = idx;
    }
    g_audio.SetPaused(true);
    UpdateThumbButtons();
}

static void PrevTrack() {
    int n = (int)g_lib.tracks.size();
    if (g_audio.HasModule() && g_audio.Position() > 3.0) {
        g_audio.Seek(0);
        g_pos = 0;
        Invalidate();
        return;
    }
    if (!n) return;
    if (g_shuffle && g_history.size() > 1) {
        g_history.pop_back();
        PlayIndex(g_history.back(), false);
        return;
    }
    int base = g_current;
    for (int a = 0; a < std::min(n, 50); ++a) {
        int idx = base <= 0 ? n - 1 : base - 1;
        if (PlayIndex(idx)) return;
        base = idx;
    }
}

static void TogglePlay();
static void OpenFolderDialog();

static void TogglePlay() {
    if (!g_audio.HasModule()) {
        if (g_lib.tracks.empty()) { OpenFolderDialog(); return; }
        int idx = g_selected >= 0 ? g_selected : (g_shuffle ? RandomOther(-1) : 0);
        if (!PlayIndex(idx)) NextTrack();
        return;
    }
    g_audio.SetPaused(!g_audio.IsPaused());
    UpdateThumbButtons();
    UpdateTaskbarProgress();
    Invalidate();
}

static void LoadFolder(const std::wstring& dir) {
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    g_lib.Scan(dir);
    SetCursor(old);
    g_history.clear();
    g_current = g_curPath.empty() ? -1 : g_lib.Find(g_curPath);
    g_selected = g_current;
    g_scroll[0] = 0;
    g_lib.StartMeta(g_hwnd, WM_APP_META);
    if (g_lib.tracks.empty()) SetStatus(L"No modules found in " + dir);
    SaveState();
    Invalidate();
}

static void OpenPath(const std::wstring& path, bool play) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        LoadFolder(path);
        g_tab = 0;
        if (play && !g_lib.tracks.empty()) PlayIndex(g_shuffle ? RandomOther(-1) : 0);
        return;
    }
    int idx = g_lib.Root().empty() ? -1 : g_lib.Find(path);
    if (idx < 0) {
        wchar_t dir[MAX_PATH * 4];
        wcscpy_s(dir, path.c_str());
        PathRemoveFileSpecW(dir);
        LoadFolder(dir);
        idx = g_lib.Find(path);
    }
    if (idx >= 0) PlayIndex(idx, true, play);
    else SetStatus(L"Not a supported module: " + std::wstring(PathFindFileNameW(path.c_str())));
}

static void OpenFolderDialog() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Choose a folder with tracker modules");
    if (!g_lib.Root().empty()) {
        IShellItem* si = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(g_lib.Root().c_str(), nullptr, IID_PPV_ARGS(&si)))) {
            dlg->SetFolder(si);
            si->Release();
        }
    }
    if (SUCCEEDED(dlg->Show(g_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                std::wstring dir = p;
                CoTaskMemFree(p);
                g_tab = 0;
                LoadFolder(dir);
            }
            item->Release();
        }
    }
    dlg->Release();
}

static void SetShuffle(bool on) { g_shuffle = on; SaveState(); Invalidate(); }
static void SetRepeat(bool on) { g_repeat = on; g_audio.SetLoop(on); SaveState(); Invalidate(); }
static void SetVolume(float v) {
    g_audio.SetVolume(v);
    if (v > 0) g_lastVolume = v;
    Invalidate();
}
static void SeekRelative(double d) {
    if (!g_audio.HasModule()) return;
    double p = std::clamp(g_audio.Position() + d, 0.0, std::max(0.0, g_info.duration - 0.1));
    g_audio.Seek(p);
    g_pos = p;
    Invalidate();
}

static void DoAction(int id) {
    switch (id) {
    case H_TAB0: g_tab = 0; ClampScroll(); SaveState(); Invalidate(); break;
    case H_TAB1: g_tab = 1; ClampScroll(); SaveState(); Invalidate(); break;
    case H_OPEN: OpenFolderDialog(); break;
    case H_SHUFFLE: SetShuffle(!g_shuffle); break;
    case H_REPEAT: SetRepeat(!g_repeat); break;
    case H_PREV: PrevTrack(); break;
    case H_PLAY: TogglePlay(); break;
    case H_NEXT: NextTrack(); break;
    case H_VOLICON: SetVolume(g_audio.Volume() > 0 ? 0.0f : (g_lastVolume > 0 ? g_lastVolume : 0.8f)); break;
    case H_SHORTCUTS:
        g_showShortcuts = !g_showShortcuts;
        g_shortcutsScroll = 0;
        Invalidate();
        break;
    case H_MODAL_CLOSE:
        g_showShortcuts = false;
        Invalidate();
        break;
    }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
static void DrawSlider(HDC, const RECT& r, float frac, bool hot, COLORREF fill) {
    int cy = (r.top + r.bottom) / 2, th = S(4);
    RECT track{r.left, cy - th / 2, r.right, cy - th / 2 + th};
    FillRR(track, 2, GC(C_BORDER));
    frac = std::clamp(frac, 0.0f, 1.0f);
    int x = r.left + (int)std::lround(frac * W(r));
    RECT done{r.left, track.top, x, track.bottom};
    if (W(done) > 0) FillRR(done, 2, GC(fill));
    int k = hot ? S(16) : S(12);
    FillCircle(CenterRect(x, cy, k, k), GC(fill));
}

static void DrawHeader(HDC dc) {
    RECT ico{rcHeader.left, rcHeader.top, rcHeader.left + S(30), rcHeader.bottom};
    Glyph(dc, G_NOTE, ico, fIcon24, C_CYAN);
    RECT t{ico.right + S(8), rcHeader.top, rcShortcuts.left - S(8), rcHeader.bottom};
    Txt(dc, L"WIN MOD PLAYER", t, fBrand, C_TEXT);

    // Shortcuts Button
    bool hotSc = (g_hover == H_SHORTCUTS);
    bool activeSc = g_showShortcuts || (g_pressed == H_SHORTCUTS && hotSc);
    FillRR(rcShortcuts, 8, activeSc ? GC(C_CYAN, 45) : (hotSc ? GC(RGB(255, 255, 255), 24) : GC(C_SURFACE)));
    StrokeRR(rcShortcuts, 8, activeSc ? GC(C_CYAN, 180) : (hotSc ? GC(C_TEXT2) : GC(C_BORDER)), 1.2);
    COLORREF scCol = activeSc ? C_CYAN : (hotSc ? C_TEXT : C_TEXT2);
    RECT cRect;
    GetClientRect(g_hwnd, &cRect);
    if (cRect.right >= S(520)) {
        int tw = TextWidth(dc, L"SHORTCUTS", fTab) + S(22);
        int x = (rcShortcuts.left + rcShortcuts.right - tw) / 2;
        RECT g{x, rcShortcuts.top, x + S(18), rcShortcuts.bottom};
        Glyph(dc, G_KEYBOARD, g, fIcon14, scCol);
        RECT tx{g.right + S(4), rcShortcuts.top, rcShortcuts.right, rcShortcuts.bottom};
        Txt(dc, L"SHORTCUTS", tx, fTab, scCol);
    } else {
        Glyph(dc, G_KEYBOARD, rcShortcuts, fIcon16, scCol);
    }

    FillRR(rcTabs, 12, GC(C_SURFACE));
    const wchar_t* names[2] = {L"PLAYLIST", L"INSTRUMENTS"};
    const wchar_t* glyphs[2] = {G_LIST, G_INSTR};
    for (int i = 0; i < 2; ++i) {
        bool sel = g_tab == i, hot = g_hover == H_TAB0 + i;
        if (sel) FillRR(rcTab[i], 8, GC(C_CYAN));
        else if (hot) FillRR(rcTab[i], 8, GC(RGB(255, 255, 255), 18));
        COLORREF col = sel ? RGB(0, 0, 0) : C_TEXT2;
        int tw = TextWidth(dc, names[i], fTab) + S(20);
        int x = (rcTab[i].left + rcTab[i].right - tw) / 2;
        RECT g{x, rcTab[i].top, x + S(16), rcTab[i].bottom};
        Glyph(dc, glyphs[i], g, fIcon14, col);
        RECT tx{x + S(20), rcTab[i].top, rcTab[i].right, rcTab[i].bottom};
        Txt(dc, names[i], tx, fTab, col);
    }
}

static void DrawInfoBadge(HDC dc, const RECT& r, const wchar_t* glyph, const wchar_t* label, const std::wstring& value,
                          COLORREF accent) {
    FillRR(r, 14, GC(C_INNER));
    StrokeRR(r, 14, GC(C_BORDER));
    RECT ib = CenterRect(r.left + S(12) + S(19), (r.top + r.bottom) / 2, S(38), S(38));
    FillRR(ib, 10, GC(accent, 38));
    StrokeRR(ib, 10, GC(accent, 100), 1);
    Glyph(dc, glyph, ib, fIcon20, accent);
    RECT l{ib.right + S(10), r.top + S(9), r.right - S(8), r.top + S(23)};
    Txt(dc, label, l, fLabel, C_TEXT2);
    RECT v{ib.right + S(10), l.bottom, r.right - S(8), r.bottom - S(7)};
    Txt(dc, value, v, fValue, C_TEXT);
}

static void DrawCard(HDC dc) {
    FillRR(rcCard, 20, GC(C_SURFACE));
    StrokeRR(rcCard, 20, GC(C_BORDER));

    // format badge
    std::wstring ext = g_hasInfo ? (g_info.type.empty() ? g_curExt : g_info.type)
                                 : (g_selected >= 0 && g_selected < (int)g_lib.tracks.size() ? g_lib.tracks[g_selected]->ext : L"");
    Fmt f = GetFmt(ext);
    RECT code{rcBadge.left, rcBadge.top, rcBadge.left + S(48), rcBadge.bottom};
    FillRR(code, 10, GC(f.color, 40));
    StrokeRR(code, 10, GC(f.color, 170), 1.5);
    Glyph(dc, f.code.c_str(), code, fCode, f.color);
    RECT n1{code.right + S(10), rcBadge.top + S(1), rcBadge.right, rcBadge.top + S(21)};
    Txt(dc, g_hasInfo && !g_info.typeLong.empty() && f.code.size() > 3 && f.name == std::wstring(L"Tracker Module")
                ? g_info.typeLong : std::wstring(f.name),
        n1, fNormalB, C_TEXT);
    RECT n2{n1.left, n1.bottom, rcBadge.right, rcBadge.bottom};
    Txt(dc, f.sub, n2, fTiny, C_TEXT2);

    // channel VU visualizer
    {
        int n = g_hasInfo ? std::clamp(g_info.channels, 1, 64) : 16;
        int bars = std::min(n, 32);
        int gap = S(bars > 16 ? 1 : 2);
        float bw = (W(rcVu) - gap * (bars - 1)) / (float)bars;
        for (int b = 0; b < bars; ++b) {
            float lvl = 0;
            int c0 = b * n / bars, c1 = std::max(c0 + 1, (b + 1) * n / bars);
            for (int c = c0; c < c1 && c < 64; ++c) lvl = std::max(lvl, g_vu[c]);
            REAL x = rcVu.left + b * (bw + gap);
            SolidBrush bg(GC(RGB(255, 255, 255), 14));
            g_gfx->FillRectangle(&bg, x, (REAL)rcVu.top, bw, (REAL)Hh(rcVu));
            REAL hgt = (REAL)(std::sqrt(std::min(lvl, 1.0f)) * Hh(rcVu));
            if (hgt >= 1) {
                SolidBrush fg(lvl > 0.9f ? GC(C_AMBER) : GC(C_CYAN, (BYTE)(140 + 115 * std::min(lvl, 1.0f))));
                g_gfx->FillRectangle(&fg, x, rcVu.bottom - hgt, bw, hgt);
            }
        }
    }

    // title & file
    std::wstring title = g_hasInfo ? (g_info.title.empty() ? g_curFile : g_info.title) : L"Select a folder or track to start";
    Txt(dc, title, rcTitle, fTitle, C_TEXT);
    RECT gi{rcFile.left, rcFile.top, rcFile.left + S(18), rcFile.bottom};
    Glyph(dc, G_DOC, gi, fIcon14, C_TEXT2);
    RECT ft{gi.right + S(6), rcFile.top, rcFile.right, rcFile.bottom};
    Txt(dc, g_hasInfo ? g_curFile : L"--", ft, fNormal, C_TEXT2);

    bool showStatus = !g_status.empty() && GetTickCount64() < g_statusUntil;
    if (showStatus || (g_hasInfo && !g_curRelDir.empty()) || (!g_hasInfo && !g_lib.Root().empty())) {
        RECT gf{rcFolder.left, rcFolder.top, rcFolder.left + S(18), rcFolder.bottom};
        COLORREF col = showStatus ? C_ERROR : C_AMBER;
        Glyph(dc, showStatus ? L"\uE7BA" : G_FOLDER_S, gf, fIcon14, col);
        RECT tt{gf.right + S(6), rcFolder.top, rcFolder.right, rcFolder.bottom};
        std::wstring s = showStatus ? g_status : (g_hasInfo ? L"/" + g_curRelDir : g_lib.Root());
        if (!showStatus) std::replace(s.begin(), s.end(), L'\\', L'/');
        Txt(dc, s, tt, fSmall, col, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
    }

    DrawInfoBadge(dc, rcInfo1, G_DRIVE, L"FILE SIZE", g_hasInfo ? FormatBytes(g_curSize) : L"0 B", C_CYAN);
    DrawInfoBadge(dc, rcInfo2, G_EQ, L"VOICES / CHANNELS",
                  (g_hasInfo ? std::to_wstring(g_info.channels) : L"0") + L" CH", C_AMBER);

    // seek bar
    double dur = g_hasInfo ? g_info.duration : 0;
    double pos = g_drag == H_SEEK ? g_dragFrac * dur : g_pos;
    float frac = dur > 0 ? (float)(pos / dur) : 0.0f;
    RECT sr{rcSeek.left + S(6), rcSeek.top, rcSeek.right - S(6), rcSeek.bottom};
    DrawSlider(dc, sr, frac, g_hover == H_SEEK || g_drag == H_SEEK, C_CYAN);
    Txt(dc, FormatTime((int)pos), rcTimes, fSmallB, C_TEXT2);
    std::wstring right = dur > 0 ? FormatTime((int)std::lround(dur)) : L"--:--";
    if (g_repeat) right = L"\u221E  " + right;
    Txt(dc, right, rcTimes, fSmallB, C_TEXT2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

static void DrawPill(HDC dc, int x, int cy, const std::wstring& s, COLORREF col) {
    int w = TextWidth(dc, s, fLabel) + S(14);
    RECT r{x, cy - S(10), x + w, cy + S(10)};
    FillRR(r, 6, GC(C_SURFACE2));
    Txt(dc, s, r, fLabel, col, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawPlaylistRow(HDC dc, int i, const RECT& r) {
    const Track& t = *g_lib.tracks[i];
    bool cur = i == g_current, sel = i == g_selected, hot = g_hover == H_LIST && g_hoverRow == i;
    RECT bg{r.left + S(4), r.top + S(2), r.right - S(12), r.bottom - S(2)};
    if (cur) {
        FillRR(bg, 10, GC(C_CYAN, 30));
        RECT bar{bg.left, bg.top + S(8), bg.left + S(3), bg.bottom - S(8)};
        FillRR(bar, 2, GC(C_CYAN));
    } else if (sel) FillRR(bg, 10, GC(C_SURFACE2));
    else if (hot) FillRR(bg, 10, GC(RGB(255, 255, 255), 10));

    RECT num{bg.left + S(6), bg.top, bg.left + S(38), bg.bottom};
    if (cur && IsPlaying()) Glyph(dc, G_PLAY, num, fIcon14, C_CYAN);
    else Txt(dc, std::to_wstring(i + 1), num, fSmall, C_TEXT3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    Fmt f = GetFmt(t.ext);
    int st = t.state.load(std::memory_order_acquire);
    std::wstring dur = st == 1 && t.duration > 0 ? FormatTime(t.duration) : (st == 2 ? L"--:--" : L"");
    RECT dr{bg.right - S(50), bg.top, bg.right - S(8), bg.bottom};
    Txt(dc, dur, dr, fSmall, C_TEXT2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT chip{dr.left - S(46), (bg.top + bg.bottom) / 2 - S(10), dr.left - S(6), (bg.top + bg.bottom) / 2 + S(10)};
    FillRR(chip, 6, GC(f.color, 36));
    Txt(dc, f.code, chip, fLabel, f.color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT tl{num.right + S(4), bg.top + S(5), chip.left - S(8), bg.top + S(25)};
    Txt(dc, t.DisplayTitle(), tl, fNormalB, cur ? C_CYAN : (st == 2 ? C_TEXT3 : C_TEXT));
    std::wstring sub = t.relDir.empty() ? t.fileName : t.relDir + L"\\" + t.fileName;
    std::replace(sub.begin(), sub.end(), L'\\', L'/');
    if (st == 1 && t.channels > 0) sub += L"  \u2022  " + std::to_wstring(t.channels) + L" ch";
    sub += L"  \u2022  " + FormatBytes(t.size);
    RECT sl{tl.left, tl.bottom, tl.right, bg.bottom - S(4)};
    Txt(dc, sub, sl, fTiny, C_TEXT2);
}

static void DrawInfoRow(HDC dc, const InfoRow& row, const RECT& r) {
    RECT tr{r.left + S(10), r.top, r.right - S(14), r.bottom};
    if (row.kind == 0) {
        Txt(dc, row.text, tr, fSmallB, C_CYAN);
    } else if (row.kind == 1) {
        Txt(dc, row.text, tr, fMono, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX | DT_EXPANDTABS);
    } else {
        wchar_t idx[16];
        swprintf_s(idx, L"%02d", row.index);
        RECT ir{tr.left, tr.top, tr.left + S(30), tr.bottom};
        Txt(dc, idx, ir, fMonoB, C_AMBER);
        RECT nr{ir.right + S(6), tr.top, tr.right, tr.bottom};
        Txt(dc, row.text, nr, fMono, row.text.find_first_not_of(L' ') == std::wstring::npos ? C_TEXT3 : C_TEXT);
    }
}

static void DrawContent(HDC dc) {
    FillRR(rcContent, 20, GC(C_SURFACE));
    StrokeRR(rcContent, 20, GC(C_BORDER));

    // header
    RECT hg{rcContentHdr.left, rcContentHdr.top, rcContentHdr.left + S(22), rcContentHdr.bottom};
    const wchar_t* title = g_tab == 0 ? L"PLAYLIST" : L"INSTRUMENTS & SAMPLE NOTES";
    Glyph(dc, g_tab == 0 ? G_LIST : G_INSTR, hg, fIcon16, g_tab == 0 ? C_CYAN : C_AMBER);
    int tw = TextWidth(dc, title, fSmallB);
    RECT ht{hg.right + S(8), rcContentHdr.top, hg.right + S(8) + tw + S(2), rcContentHdr.bottom};
    int maxRight = g_tab == 0 ? rcOpen.left - S(8) : rcContentHdr.right;
    if (ht.right > maxRight) ht.right = maxRight;
    Txt(dc, title, ht, fSmallB, C_TEXT);
    int cy = (rcContentHdr.top + rcContentHdr.bottom) / 2;
    if (g_tab == 0) {
        std::wstring cnt = std::to_wstring(g_lib.tracks.size()) + L" tracks";
        if (ht.right + S(10) + TextWidth(dc, cnt, fLabel) + S(14) < rcOpen.left - S(6)) DrawPill(dc, ht.right + S(10), cy, cnt, C_CYAN);
        bool hot = g_hover == H_OPEN;
        FillRR(rcOpen, 9, hot ? GC(C_CYAN, 60) : GC(C_CYAN, 28));
        StrokeRR(rcOpen, 9, GC(C_CYAN, 120), 1);
        int bw = S(18) + S(6) + TextWidth(dc, L"OPEN FOLDER", fTab);
        int x = (rcOpen.left + rcOpen.right - bw) / 2;
        RECT g{x, rcOpen.top, x + S(18), rcOpen.bottom};
        Glyph(dc, G_FOLDER, g, fIcon14, C_CYAN);
        RECT t{g.right + S(6), rcOpen.top, rcOpen.right, rcOpen.bottom};
        Txt(dc, L"OPEN FOLDER", t, fTab, C_CYAN);
    } else if (g_hasInfo) {
        std::wstring cnt = std::to_wstring(g_info.instruments.size() + g_info.samples.size()) + L" entries";
        if (ht.right + S(10) + TextWidth(dc, cnt, fLabel) + S(14) < rcContentHdr.right) DrawPill(dc, ht.right + S(10), cy, cnt, C_AMBER);
    }

    // list
    size_t count = g_tab == 0 ? g_lib.tracks.size() : g_infoRows.size();
    if (!count) {
        RECT big{rcList.left, rcList.top, rcList.right, (rcList.top + rcList.bottom) / 2 + S(4)};
        Glyph(dc, g_tab == 0 ? G_FOLDER : G_NOTE, big, fIconBig, RGB(0x3A, 0x44, 0x55));
        RECT msg{rcList.left + S(10), big.bottom + S(4), rcList.right - S(10), big.bottom + S(44)};
        Txt(dc, g_tab == 0 ? L"Open a folder or drop modules / a folder here" : L"No instrument text or comments found",
            msg, fNormal, C_TEXT3, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        return;
    }
    g_gfx->Flush(FlushIntentionSync);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, rcList.left, rcList.top, rcList.right, rcList.bottom);
    g_gfx->SetClip(Rect(rcList.left, rcList.top, W(rcList), Hh(rcList)));
    int rh = RowH(), first = g_scroll[g_tab] / rh;
    for (int i = first; i < (int)count; ++i) {
        int y = rcList.top + i * rh - g_scroll[g_tab];
        if (y >= rcList.bottom) break;
        RECT r{rcList.left, y, rcList.right, y + rh};
        if (g_tab == 0) DrawPlaylistRow(dc, i, r);
        else DrawInfoRow(dc, g_infoRows[i], r);
    }
    g_gfx->Flush(FlushIntentionSync);
    g_gfx->ResetClip();
    RestoreDC(dc, saved);

    RECT th;
    if (ScrollThumb(th)) {
        bool hot = g_hover == H_SCROLL || g_drag == H_SCROLL;
        FillRR(th, 3, GC(hot ? C_TEXT2 : C_BORDER));
    }
}

static void DrawRoundButton(HDC dc, const RECT& r, const wchar_t* glyph, HFONT f, int id, bool active) {
    bool hot = g_hover == id, down = g_pressed == id && hot;
    if (active) FillCircle(r, GC(C_CYAN, down ? 70 : 40));
    else if (hot) FillCircle(r, GC(RGB(255, 255, 255), down ? 34 : 18));
    Glyph(dc, glyph, r, f, active ? C_CYAN : (hot ? C_TEXT : C_TEXT2));
}

static void DrawControls(HDC dc) {
    SolidBrush b(GC(C_SURFACE));
    g_gfx->FillRectangle(&b, (REAL)rcControls.left, (REAL)rcControls.top, (REAL)W(rcControls), (REAL)Hh(rcControls));
    Pen p(GC(C_BORDER), (REAL)std::max(1, S(1)));
    g_gfx->DrawLine(&p, (REAL)rcControls.left, (REAL)rcControls.top, (REAL)rcControls.right, (REAL)rcControls.top);

    DrawRoundButton(dc, rcShuffle, G_SHUFFLE, fIcon16, H_SHUFFLE, g_shuffle);
    DrawRoundButton(dc, rcRepeat, G_REPEAT1, fIcon16, H_REPEAT, g_repeat);
    DrawRoundButton(dc, rcPrev, G_PREV, fIcon20, H_PREV, false);
    DrawRoundButton(dc, rcNext, G_NEXT, fIcon20, H_NEXT, false);

    bool hot = g_hover == H_PLAY, down = hot && g_pressed == H_PLAY;
    if (hot) FillCircle(CenterRect((rcPlay.left + rcPlay.right) / 2, (rcPlay.top + rcPlay.bottom) / 2, W(rcPlay) + S(10), W(rcPlay) + S(10)), GC(C_CYAN, 40));
    FillCircle(rcPlay, down ? GC(RGB(0x00, 0xB8, 0xD4)) : GC(C_CYAN));
    Glyph(dc, IsPlaying() ? G_PAUSE : G_PLAY, rcPlay, fIcon24, RGB(0, 0, 0));

    float v = g_audio.Volume();
    DrawRoundButton(dc, rcVolIcon, v <= 0 ? G_MUTE : G_VOLUME, fIcon16, H_VOLICON, false);
    DrawSlider(dc, rcVol, v, g_hover == H_VOL || g_drag == H_VOL, C_TEXT2);
}

static void DrawShortcutsModal(HDC dc, const RECT& client) {
    // 1. Semi-transparent backdrop overlay
    SolidBrush veil(Color(200, 10, 14, 20));
    g_gfx->FillRectangle(&veil, (REAL)client.left, (REAL)client.top, (REAL)W(client), (REAL)Hh(client));

    // 2. Modal card dimensions
    int cardW = std::min(W(client) - S(32), S(450));
    int cardH = std::min(Hh(client) - S(48), S(580));
    rcModalCard = CenterRect((client.left + client.right) / 2, (client.top + client.bottom) / 2, cardW, cardH);

    // Modal background & double glowing cyber border
    FillRR(rcModalCard, 18, GC(C_SURFACE));
    StrokeRR(rcModalCard, 18, GC(C_BORDER), 1.5);
    StrokeRR(rcModalCard, 18, GC(C_CYAN, 80), 1.0);

    // Header inside modal
    int hdrH = S(48);
    RECT mHdr = { rcModalCard.left + S(16), rcModalCard.top + S(4), rcModalCard.right - S(14), rcModalCard.top + hdrH };

    // Keyboard icon
    RECT mIco = { mHdr.left, mHdr.top, mHdr.left + S(28), mHdr.bottom };
    Glyph(dc, G_KEYBOARD, mIco, fIcon20, C_CYAN);

    // Title text
    RECT mTitle = { mIco.right + S(8), mHdr.top, mHdr.right - S(40), mHdr.bottom };
    Txt(dc, L"HANDY SHORTCUTS", mTitle, fNormalB, C_TEXT);

    // Close button (X)
    rcModalClose = CenterRect(mHdr.right - S(16), (mHdr.top + mHdr.bottom) / 2, S(28), S(28));
    bool hotClose = (g_hover == H_MODAL_CLOSE);
    if (hotClose) FillCircle(rcModalClose, GC(C_CYAN, 40));
    Glyph(dc, L"\u2715", rcModalClose, fSmallB, hotClose ? C_CYAN : C_TEXT2);

    // Separator line
    Pen sep(GC(C_BORDER), (REAL)std::max(1, S(1)));
    g_gfx->DrawLine(&sep, (REAL)rcModalCard.left + S(16), (REAL)rcModalCard.top + hdrH, 
                          (REAL)rcModalCard.right - S(16), (REAL)rcModalCard.top + hdrH);

    // Modal content list
    rcModalContent = { rcModalCard.left + S(16), rcModalCard.top + hdrH + S(8), rcModalCard.right - S(16), rcModalCard.bottom - S(14) };

    // Calculate total height
    int catH = S(28), rowH = S(26);
    int totalH = 0;
    for (const auto& item : g_shortcutsList) {
        totalH += item.isCategory ? catH : rowH;
    }

    int maxScroll = std::max(0, totalH - Hh(rcModalContent));
    g_shortcutsScroll = std::clamp(g_shortcutsScroll, 0, maxScroll);

    g_gfx->Flush(FlushIntentionSync);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, rcModalContent.left, rcModalContent.top, rcModalContent.right, rcModalContent.bottom);
    g_gfx->SetClip(Rect(rcModalContent.left, rcModalContent.top, W(rcModalContent), Hh(rcModalContent)));

    int curY = rcModalContent.top - g_shortcutsScroll;
    for (const auto& item : g_shortcutsList) {
        int itemH = item.isCategory ? catH : rowH;
        if (curY + itemH >= rcModalContent.top && curY < rcModalContent.bottom) {
            RECT rItem = { rcModalContent.left, curY, rcModalContent.right, curY + itemH };
            if (item.isCategory) {
                RECT rCat = { rItem.left + S(4), rItem.top + S(6), rItem.right, rItem.bottom };
                Txt(dc, item.key, rCat, fLabel, C_AMBER);
            } else {
                // Key cap badge
                int keyW = TextWidth(dc, item.key, fMonoB) + S(16);
                int badgeW = std::max(keyW, S(80));
                RECT rKey = { rItem.left + S(4), rItem.top + S(2), rItem.left + S(4) + badgeW, rItem.bottom - S(2) };
                FillRR(rKey, 6, GC(C_INNER));
                StrokeRR(rKey, 6, GC(C_BORDER), 1.0);
                Txt(dc, item.key, rKey, fMonoB, C_CYAN, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                // Description
                RECT rDesc = { rKey.right + S(12), rItem.top, rItem.right - S(8), rItem.bottom };
                Txt(dc, item.desc, rDesc, fSmall, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        curY += itemH;
    }

    g_gfx->Flush(FlushIntentionSync);
    g_gfx->ResetClip();
    RestoreDC(dc, saved);

    // Scrollbar for modal if needed
    if (totalH > Hh(rcModalContent) && maxScroll > 0) {
        RECT tr = { rcModalCard.right - S(10), rcModalContent.top, rcModalCard.right - S(5), rcModalContent.bottom };
        int thH = std::max(S(24), Hh(tr) * Hh(rcModalContent) / totalH);
        int ty = tr.top + (int)((long long)(Hh(tr) - thH) * g_shortcutsScroll / maxScroll);
        RECT th = { tr.left, ty, tr.right, ty + thH };
        FillRR(th, 3, GC(C_BORDER));
    }
}

static void Paint(HDC dc, const RECT& client) {
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g_gfx = &g;
    DrawHeader(dc);
    DrawCard(dc);
    DrawContent(dc);
    DrawControls(dc);
    if (g_showShortcuts) {
        DrawShortcutsModal(dc, client);
    }
    g.Flush(FlushIntentionSync);
    g_gfx = nullptr;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static int HitTest(POINT p, int* row = nullptr) {
    if (row) *row = -1;
    if (g_showShortcuts) {
        if (In(rcModalClose, p)) return H_MODAL_CLOSE;
        if (In(rcModalCard, p)) return H_NONE;
        return H_MODAL_CLOSE;
    }
    if (In(rcShortcuts, p)) return H_SHORTCUTS;
    if (In(rcTab[0], p)) return H_TAB0;
    if (In(rcTab[1], p)) return H_TAB1;
    RECT seekHit{rcSeek.left, rcSeek.top - S(2), rcSeek.right, rcSeek.bottom + S(2)};
    if (In(seekHit, p) && g_hasInfo) return H_SEEK;
    if (g_tab == 0 && In(rcOpen, p)) return H_OPEN;
    if (In(rcShuffle, p)) return H_SHUFFLE;
    if (In(rcRepeat, p)) return H_REPEAT;
    if (In(rcPrev, p)) return H_PREV;
    if (In(rcPlay, p)) return H_PLAY;
    if (In(rcNext, p)) return H_NEXT;
    if (In(rcVolIcon, p)) return H_VOLICON;
    RECT volHit{rcVol.left - S(6), rcVol.top - S(4), rcVol.right + S(2), rcVol.bottom + S(4)};
    if (In(volHit, p)) return H_VOL;
    if (In(rcList, p)) {
        RECT th, tr = ScrollTrack();
        tr.left -= S(4);
        if (ScrollThumb(th) && In(tr, p)) return H_SCROLL;
        int idx = (p.y - rcList.top + g_scroll[g_tab]) / RowH();
        int count = (int)(g_tab == 0 ? g_lib.tracks.size() : g_infoRows.size());
        if (row) *row = idx < count ? idx : -1;
        return H_LIST;
    }
    return H_NONE;
}

static float FracIn(const RECT& r, int x, int inset = 0) {
    int l = r.left + inset, w = W(r) - 2 * inset;
    return w > 0 ? std::clamp((x - l) / (float)w, 0.0f, 1.0f) : 0.0f;
}

static void OnMouseDown(POINT p, bool dbl) {
    int row;
    int h = HitTest(p, &row);
    SetFocus(g_hwnd);
    switch (h) {
    case H_SEEK:
        g_drag = H_SEEK;
        g_dragFrac = FracIn(rcSeek, p.x, S(6));
        SetCapture(g_hwnd);
        break;
    case H_VOL:
        g_drag = H_VOL;
        SetCapture(g_hwnd);
        SetVolume(FracIn(rcVol, p.x));
        break;
    case H_SCROLL: {
        RECT th;
        if (ScrollThumb(th) && !In(th, p)) {  // page jump towards click
            g_scroll[g_tab] += (p.y < th.top ? -1 : 1) * Hh(rcList);
            ClampScroll();
        }
        g_drag = H_SCROLL;
        g_dragStartY = p.y;
        g_dragStartScroll = g_scroll[g_tab];
        SetCapture(g_hwnd);
        break;
    }
    case H_LIST:
        if (g_tab == 0 && row >= 0) {
            g_selected = row;
            if (dbl) PlayIndex(row);
        }
        break;
    case H_NONE: break;
    default:
        g_pressed = h;
        SetCapture(g_hwnd);
        break;
    }
    Invalidate();
}

static void OnMouseMove(POINT p) {
    if (!g_trackingMouse) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, g_hwnd, 0};
        TrackMouseEvent(&tme);
        g_trackingMouse = true;
    }
    if (g_drag == H_SEEK) { g_dragFrac = FracIn(rcSeek, p.x, S(6)); Invalidate(); return; }
    if (g_drag == H_VOL) { SetVolume(FracIn(rcVol, p.x)); return; }
    if (g_drag == H_SCROLL) {
        RECT th, tr = ScrollTrack();
        if (ScrollThumb(th)) {
            int range = Hh(tr) - Hh(th);
            if (range > 0) g_scroll[g_tab] = g_dragStartScroll + (int)((long long)(p.y - g_dragStartY) * MaxScroll() / range);
            ClampScroll();
        }
        Invalidate();
        return;
    }
    int row;
    int h = HitTest(p, &row);
    if (h != g_hover || row != g_hoverRow) {
        g_hover = h;
        g_hoverRow = row;
        Invalidate();
    }
}

static void OnMouseUp(POINT p) {
    if (g_drag == H_SEEK) {
        if (g_hasInfo) {
            double t = g_dragFrac * g_info.duration;
            g_audio.Seek(t);
            g_pos = t;
        }
    }
    int pressed = g_pressed;
    g_drag = H_NONE;
    g_pressed = H_NONE;
    if (GetCapture() == g_hwnd) ReleaseCapture();
    if (pressed != H_NONE && HitTest(p) == pressed) DoAction(pressed);
    Invalidate();
}

static void OnWheel(POINT screenPt, int delta) {
    POINT p = screenPt;
    ScreenToClient(g_hwnd, &p);
    if (g_showShortcuts) {
        int catH = S(28), rowH = S(26);
        int totalH = 0;
        for (const auto& item : g_shortcutsList) totalH += item.isCategory ? catH : rowH;
        int maxScroll = std::max(0, totalH - Hh(rcModalContent));
        g_shortcutsScroll = std::clamp(g_shortcutsScroll - (delta * rowH * 3) / WHEEL_DELTA, 0, maxScroll);
        Invalidate();
        return;
    }
    RECT volArea{rcVolIcon.left, rcControls.top, rcVol.right, rcControls.bottom};
    if (In(volArea, p)) {
        SetVolume(std::clamp(g_audio.Volume() + 0.05f * delta / WHEEL_DELTA, 0.0f, 1.0f));
        return;
    }
    if (In(rcSeek, p)) { SeekRelative(5.0 * delta / WHEEL_DELTA); return; }
    g_scroll[g_tab] -= delta * (g_tab == 0 ? S(48) * 3 : S(21) * 5) / WHEEL_DELTA;
    ClampScroll();
    OnMouseMove(p);
    Invalidate();
}

static void MoveSelection(int d) {
    int n = (int)g_lib.tracks.size();
    if (g_tab != 0) {
        g_scroll[1] += d * S(21);
        ClampScroll();
        Invalidate();
        return;
    }
    if (!n) return;
    int s = g_selected < 0 ? (g_current < 0 ? 0 : g_current) : g_selected + d;
    g_selected = std::clamp(s, 0, n - 1);
    EnsureVisible(g_selected);
    Invalidate();
}

static void OnKey(WPARAM vk) {
    if (vk == VK_F1 || vk == VK_OEM_2) { // F1 or '?'
        g_showShortcuts = !g_showShortcuts;
        g_shortcutsScroll = 0;
        Invalidate();
        return;
    }
    if (g_showShortcuts) {
        if (vk == VK_ESCAPE || vk == VK_SPACE || vk == VK_RETURN) {
            g_showShortcuts = false;
            Invalidate();
            return;
        }
        int rowH = S(26);
        if (vk == VK_UP) { g_shortcutsScroll = std::max(0, g_shortcutsScroll - rowH); Invalidate(); return; }
        if (vk == VK_DOWN) { g_shortcutsScroll += rowH; Invalidate(); return; }
        if (vk == VK_PRIOR) { g_shortcutsScroll = std::max(0, g_shortcutsScroll - Hh(rcModalContent)); Invalidate(); return; }
        if (vk == VK_NEXT) { g_shortcutsScroll += Hh(rcModalContent); Invalidate(); return; }
        return;
    }
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    int page = std::max(1, Hh(rcList) / RowH() - 1);
    switch (vk) {
    case VK_SPACE: TogglePlay(); break;
    case VK_LEFT: if (ctrl) PrevTrack(); else SeekRelative(-5); break;
    case VK_RIGHT: if (ctrl) NextTrack(); else SeekRelative(5); break;
    case VK_UP: MoveSelection(-1); break;
    case VK_DOWN: MoveSelection(1); break;
    case VK_PRIOR: MoveSelection(-page); break;
    case VK_NEXT: MoveSelection(page); break;
    case VK_HOME: MoveSelection(-1000000); break;
    case VK_END: MoveSelection(1000000); break;
    case VK_RETURN: if (g_tab == 0 && g_selected >= 0) PlayIndex(g_selected); break;
    case VK_TAB: DoAction(g_tab == 0 ? H_TAB1 : H_TAB0); break;
    case 'N': NextTrack(); break;
    case 'P': case 'B': PrevTrack(); break;
    case 'S': SetShuffle(!g_shuffle); break;
    case 'R': SetRepeat(!g_repeat); break;
    case 'M': DoAction(H_VOLICON); break;
    case 'O': OpenFolderDialog(); break;
    case 'L': if (g_current >= 0) { g_tab = 0; EnsureVisible(g_current); Invalidate(); } break;
    case VK_F5: if (!g_lib.Root().empty()) LoadFolder(g_lib.Root()); break;
    case VK_ADD: case VK_OEM_PLUS: SetVolume(std::min(1.0f, g_audio.Volume() + 0.05f)); break;
    case VK_SUBTRACT: case VK_OEM_MINUS: SetVolume(std::max(0.0f, g_audio.Volume() - 0.05f)); break;
    }
}

static void OnTimer() {
    bool playing = IsPlaying();
    float lv[64];
    int n = playing ? g_audio.ChannelLevels(lv, 64) : 0;
    bool any = false;
    for (int i = 0; i < 64; ++i) {
        float l = i < n ? lv[i] : 0.0f;
        g_vu[i] = std::max(l, g_vu[i] * 0.82f);
        if (g_vu[i] < 0.01f) g_vu[i] = 0;
        any |= g_vu[i] > 0;
    }
    if (g_audio.HasModule()) {
        double p = g_audio.Position();
        if (p != g_pos) { g_pos = p; any = true; }
    }
    if (!g_status.empty() && GetTickCount64() >= g_statusUntil) { g_status.clear(); any = true; }
    if (any || playing) InvalidateRect(g_hwnd, &rcCard, FALSE);
    if (GetTickCount64() - g_lastTaskbarUpdate > 500) {
        g_lastTaskbarUpdate = GetTickCount64();
        UpdateTaskbarProgress();
    }
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
static void ApplyDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF cap = C_BG, txt = C_TEXT;
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
    DwmSetWindowAttribute(hwnd, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof(txt));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_wmTaskbarCreated && g_wmTaskbarCreated) { InitTaskbar(); return 0; }
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        g_dpi = GetDpiForWindow(hwnd);
        CreateFonts();
        ApplyDarkTitleBar(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        RegisterHotKey(hwnd, 1, MOD_NOREPEAT, VK_MEDIA_PLAY_PAUSE);
        RegisterHotKey(hwnd, 2, MOD_NOREPEAT, VK_MEDIA_NEXT_TRACK);
        RegisterHotKey(hwnd, 3, MOD_NOREPEAT, VK_MEDIA_PREV_TRACK);
        RegisterHotKey(hwnd, 4, MOD_NOREPEAT, VK_MEDIA_STOP);
        SetTimer(hwnd, TIMER_UI, 33, nullptr);
        return 0;
    case WM_SIZE:
        Layout();
        Invalidate();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        RECT r{0, 0, S(380), S(600)};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, g_dpi);
        mmi->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        CreateFonts();
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        Invalidate();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT c;
        GetClientRect(hwnd, &c);
        if (!g_memDC || g_memSize.cx != c.right || g_memSize.cy != c.bottom) {
            if (g_memDC) { DeleteDC(g_memDC); DeleteObject(g_memBmp); }
            g_memDC = CreateCompatibleDC(dc);
            g_memBmp = CreateCompatibleBitmap(dc, std::max(1L, c.right), std::max(1L, c.bottom));
            SelectObject(g_memDC, g_memBmp);
            g_memSize = {c.right, c.bottom};
        }
        Paint(g_memDC, c);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, W(ps.rcPaint), Hh(ps.rcPaint), g_memDC, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_UI) OnTimer();
        return 0;
    case WM_LBUTTONDOWN: OnMouseDown({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, false); return 0;
    case WM_LBUTTONDBLCLK: OnMouseDown({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, true); return 0;
    case WM_LBUTTONUP: OnMouseUp({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_MOUSEMOVE: OnMouseMove({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_MOUSELEAVE:
        g_trackingMouse = false;
        if (g_drag == H_NONE) { g_hover = H_NONE; g_hoverRow = -1; Invalidate(); }
        return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)lp != hwnd) { g_drag = H_NONE; g_pressed = H_NONE; }
        return 0;
    case WM_MOUSEWHEEL: OnWheel({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, GET_WHEEL_DELTA_WPARAM(wp)); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            bool hand = g_hover != H_NONE && g_hover != H_LIST;
            SetCursor(LoadCursor(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_KEYDOWN: OnKey(wp); return 0;
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_HOTKEY:
        switch (wp) {
        case 1: TogglePlay(); break;
        case 2: NextTrack(); break;
        case 3: PrevTrack(); break;
        case 4: if (g_audio.HasModule()) { g_audio.SetPaused(true); g_audio.Seek(0); g_pos = 0; UpdateThumbButtons(); Invalidate(); } break;
        }
        return 0;
    case WM_APPCOMMAND:
        switch (GET_APPCOMMAND_LPARAM(lp)) {
        case APPCOMMAND_MEDIA_PLAY_PAUSE: TogglePlay(); return TRUE;
        case APPCOMMAND_MEDIA_PLAY: if (!IsPlaying()) TogglePlay(); return TRUE;
        case APPCOMMAND_MEDIA_PAUSE: if (IsPlaying()) TogglePlay(); return TRUE;
        case APPCOMMAND_MEDIA_NEXTTRACK: NextTrack(); return TRUE;
        case APPCOMMAND_MEDIA_PREVIOUSTRACK: PrevTrack(); return TRUE;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wp) == THBN_CLICKED) {
            switch (LOWORD(wp)) {
            case THB_PREV: PrevTrack(); break;
            case THB_PLAY: TogglePlay(); break;
            case THB_NEXT: NextTrack(); break;
            }
            return 0;
        }
        break;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        wchar_t path[MAX_PATH * 4];
        if (DragQueryFileW(drop, 0, path, ARRAYSIZE(path))) OpenPath(path, true);
        DragFinish(drop);
        SetForegroundWindow(hwnd);
        return 0;
    }
    case WM_COPYDATA: {  // path forwarded from a second instance
        auto* cds = (COPYDATASTRUCT*)lp;
        if (cds->dwData == 0x4D4F4450 && cds->cbData >= sizeof(wchar_t)) {
            std::wstring p((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t));
            while (!p.empty() && p.back() == 0) p.pop_back();
            OpenPath(p, true);
        }
        return TRUE;
    }
    case WM_APP_TRACKEND:
        if (!g_repeat) NextTrack();
        return 0;
    case WM_APP_META:
        Invalidate();
        return 0;
    case WM_CLOSE:
        SaveWindowPlacement();
        SaveState();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_UI);
        for (int i = 1; i <= 4; ++i) UnregisterHotKey(hwnd, i);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    g_inst = hInst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring argPath = argc > 1 ? argv[1] : L"";
    LocalFree(argv);

    // Single instance: forward the file to the running player.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"WinModPlayer.SingleInstance.7F3A");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"WinModPlayerWnd", nullptr);
        if (other) {
            if (!argPath.empty()) {
                wchar_t full[MAX_PATH * 4];
                GetFullPathNameW(argPath.c_str(), ARRAYSIZE(full), full, nullptr);
                COPYDATASTRUCT cds{0x4D4F4450, (DWORD)((wcslen(full) + 1) * sizeof(wchar_t)), full};
                SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cds);
            }
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
            return 0;
        }
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gsi;
    ULONG_PTR gdipToken = 0;
    GdiplusStartup(&gdipToken, &gsi, nullptr);

    if (FontExists(L"Segoe UI Variable Text")) g_uiFace = L"Segoe UI Variable Text";
    if (FontExists(L"Segoe Fluent Icons")) g_iconFace = L"Segoe Fluent Icons";
    InitSettingsPath();
    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
    ChangeWindowMessageFilter(g_wmTaskbarCreated, MSGFLT_ADD);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"WinModPlayerWnd";
    RegisterClassExW(&wc);

    int x = GetSettingInt(L"X", CW_USEDEFAULT), y = GetSettingInt(L"Y", CW_USEDEFAULT);
    int w = GetSettingInt(L"W", 0), h = GetSettingInt(L"H", 0);
    bool havePos = w > 0 && h > 0;
    if (havePos) {  // make sure the saved rect is still on a monitor
        RECT r{x, y, x + w, y + h};
        if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) havePos = false;
    }
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"WinModPlayer", WS_OVERLAPPEDWINDOW,
                                havePos ? x : CW_USEDEFAULT, havePos ? y : CW_USEDEFAULT,
                                havePos ? w : 480, havePos ? h : 800, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    if (!havePos) {
        RECT r{0, 0, S(470), S(780)};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, g_dpi);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        int ww = r.right - r.left, wh = std::min<int>(r.bottom - r.top, mi.rcWork.bottom - mi.rcWork.top);
        SetWindowPos(hwnd, nullptr, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2,
                     mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - wh) / 2, ww, wh, SWP_NOZORDER);
    }

    // restore state
    g_shuffle = GetSettingInt(L"Shuffle", 0) != 0;
    g_repeat = GetSettingInt(L"Repeat", 0) != 0;
    g_tab = std::clamp(GetSettingInt(L"Tab", 0), 0, 1);
    float vol = std::clamp(GetSettingInt(L"Volume", 80), 0, 100) / 100.0f;
    g_audio.SetVolume(vol);
    if (vol > 0) g_lastVolume = vol;
    g_audio.SetLoop(g_repeat);
    g_audio.Start(hwnd, WM_APP_TRACKEND);

    Layout();
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    if (!argPath.empty()) {
        wchar_t full[MAX_PATH * 4];
        GetFullPathNameW(argPath.c_str(), ARRAYSIZE(full), full, nullptr);
        OpenPath(full, true);
    } else {
        std::wstring folder = GetSetting(L"Folder");
        if (!folder.empty() && PathIsDirectoryW(folder.c_str())) {
            LoadFolder(folder);
            int idx = g_lib.Find(GetSetting(L"Track"));
            if (idx >= 0) PlayIndex(idx, true, false);  // ready, but paused
        }
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    g_lib.StopMeta();
    g_audio.Stop();
    if (g_taskbar) g_taskbar->Release();
    DestroyFonts();
    if (g_memDC) { DeleteDC(g_memDC); DeleteObject(g_memBmp); }
    GdiplusShutdown(gdipToken);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
