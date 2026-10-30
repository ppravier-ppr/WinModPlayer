// playlist.h - folder scanning + background metadata extraction
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct Track {
    std::wstring path;      // full path
    std::wstring fileName;  // name.ext
    std::wstring relDir;    // directory relative to library root ("" = root)
    std::wstring ext;       // lowercase, without dot
    std::uint64_t size = 0;

    // Filled by the metadata worker. Read 'state' (acquire) before reading the fields.
    std::wstring title;
    int channels = 0;
    int duration = 0;
    std::atomic<int> state{0};  // 0 = pending, 1 = ok, 2 = failed

    std::wstring DisplayTitle() const;
};

class Library {
public:
    ~Library() { StopMeta(); }

    // Recursively scans 'root' for supported modules (replaces current content).
    void Scan(const std::wstring& root);
    void Clear();

    // Starts background metadata extraction; posts 'msg' to 'wnd' periodically.
    void StartMeta(HWND wnd, UINT msg);
    void StopMeta();

    int Find(const std::wstring& path) const;
    const std::wstring& Root() const { return root_; }

    std::vector<std::unique_ptr<Track>> tracks;

    static bool IsSupportedExtension(const std::wstring& extLower);

private:
    std::wstring root_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
};

std::wstring FormatBytes(std::uint64_t bytes);
std::wstring FormatTime(int seconds);
