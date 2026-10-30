// playlist.cpp
#include "playlist.h"
#include "audio.h"

#include <shlwapi.h>
#include <libopenmpt/libopenmpt.hpp>

#include <algorithm>
#include <cwctype>
#include <map>
#include <mutex>
#include <ostream>
#include <set>

static std::ostream g_nullLog2(nullptr);

static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

std::wstring Track::DisplayTitle() const {
    if (state.load(std::memory_order_acquire) == 1 && !title.empty()) return title;
    size_t dot = fileName.find_last_of(L'.');
    return dot == std::wstring::npos ? fileName : fileName.substr(0, dot);
}

bool Library::IsSupportedExtension(const std::wstring& extLower) {
    static std::set<std::wstring> exts;
    static std::once_flag once;
    std::call_once(once, [] {
        for (auto& e : openmpt::get_supported_extensions()) exts.insert(Lower(Utf8ToWide(e)));
    });
    return exts.count(extLower) != 0;
}

void Library::Clear() {
    StopMeta();
    tracks.clear();
    root_.clear();
}

static void ScanDir(const std::wstring& root, const std::wstring& rel, std::vector<std::unique_ptr<Track>>& out, int depth) {
    if (depth > 32) return;
    std::wstring dir = rel.empty() ? root : root + L"\\" + rel;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> subdirs;
    do {
        const wchar_t* n = fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(n, L".") && wcscmp(n, L"..") && !(fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                subdirs.push_back(n);
            continue;
        }
        const wchar_t* dot = wcsrchr(n, L'.');
        std::wstring ext;
        if (dot) ext = Lower(dot + 1);
        // Amiga style prefix names ("mod.songname")
        const wchar_t* firstDot = wcschr(n, L'.');
        std::wstring prefix = firstDot ? Lower(std::wstring(n, firstDot - n)) : L"";
        bool ok = !ext.empty() && Library::IsSupportedExtension(ext);
        if (!ok && !prefix.empty() && Library::IsSupportedExtension(prefix)) { ok = true; ext = prefix; }
        if (!ok) continue;
        auto t = std::make_unique<Track>();
        t->fileName = n;
        t->relDir = rel;
        t->path = dir + L"\\" + n;
        t->ext = ext;
        t->size = ((std::uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        out.push_back(std::move(t));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (auto& s : subdirs) ScanDir(root, rel.empty() ? s : rel + L"\\" + s, out, depth + 1);
}

void Library::Scan(const std::wstring& root) {
    StopMeta();
    tracks.clear();
    root_ = root;
    while (root_.size() > 3 && (root_.back() == L'\\' || root_.back() == L'/')) root_.pop_back();
    if (root_.size() == 3 && root_.back() == L'\\') root_.pop_back();  // "C:\" -> "C:"
    ScanDir(root_, L"", tracks, 0);
    std::stable_sort(tracks.begin(), tracks.end(), [](const auto& a, const auto& b) {
        int c = StrCmpLogicalW(a->relDir.c_str(), b->relDir.c_str());
        if (c) return c < 0;
        return StrCmpLogicalW(a->fileName.c_str(), b->fileName.c_str()) < 0;
    });
}

int Library::Find(const std::wstring& path) const {
    for (size_t i = 0; i < tracks.size(); ++i)
        if (_wcsicmp(tracks[i]->path.c_str(), path.c_str()) == 0) return (int)i;
    return -1;
}

void Library::StartMeta(HWND wnd, UINT msg) {
    StopMeta();
    stop_ = false;
    worker_ = std::thread([this, wnd, msg] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        DWORD lastPost = GetTickCount();
        std::vector<std::uint8_t> data;
        for (auto& tp : tracks) {
            if (stop_) return;
            Track& t = *tp;
            int st = 2;
            if (ReadWholeFile(t.path, data, 64u << 20)) {
                try {
                    std::map<std::string, std::string> ctls{{"load.skip_samples", "1"}};
                    openmpt::module m(data.data(), data.size(), g_nullLog2, ctls);
                    t.title = Utf8ToWide(m.get_metadata("title"));
                    // trim trailing spaces of tracker titles
                    while (!t.title.empty() && iswspace(t.title.back())) t.title.pop_back();
                    t.channels = m.get_num_channels();
                    t.duration = (int)(m.get_duration_seconds() + 0.5);
                    st = 1;
                } catch (...) {
                }
            }
            t.state.store(st, std::memory_order_release);
            if (GetTickCount() - lastPost > 250) {
                PostMessageW(wnd, msg, 0, 0);
                lastPost = GetTickCount();
            }
        }
        PostMessageW(wnd, msg, 1, 0);
    });
}

void Library::StopMeta() {
    if (worker_.joinable()) {
        stop_ = true;
        worker_.join();
    }
}

std::wstring FormatBytes(std::uint64_t b) {
    wchar_t buf[64];
    if (b < 1024) swprintf_s(buf, L"%llu B", (unsigned long long)b);
    else if (b < 1024ull * 1024) swprintf_s(buf, L"%.1f KB", b / 1024.0);
    else swprintf_s(buf, L"%.1f MB", b / (1024.0 * 1024.0));
    return buf;
}

std::wstring FormatTime(int s) {
    if (s < 0) s = 0;
    wchar_t buf[32];
    if (s >= 3600) swprintf_s(buf, L"%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else swprintf_s(buf, L"%d:%02d", s / 60, s % 60);
    return buf;
}
