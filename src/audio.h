// audio.h - libopenmpt playback through WASAPI (shared mode, event driven)
#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace openmpt { class module; }

struct ModuleInfo {
    std::wstring title, type, typeLong, tracker, artist, message;
    int channels = 0, patterns = 0, orders = 0, instrumentsCount = 0, samplesCount = 0;
    double duration = 0.0;
    std::vector<std::wstring> instruments, samples;
};

std::wstring Utf8ToWide(const std::string& s);
bool ReadWholeFile(const std::wstring& path, std::vector<std::uint8_t>& out, size_t maxBytes = 256u << 20);

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // notifyWnd receives endMsg (posted) when the current module finished playing.
    bool Start(HWND notifyWnd, UINT endMsg);
    void Stop();

    bool Load(const std::wstring& path, ModuleInfo& info, std::wstring& error);
    void Unload();
    bool HasModule() const { return hasModule_.load(); }

    void SetPaused(bool p) { paused_.store(p); }
    bool IsPaused() const { return paused_.load(); }

    double Position();
    void Seek(double seconds);

    void SetVolume(float v) { volume_.store(v < 0 ? 0 : (v > 1 ? 1 : v)); }
    float Volume() const { return volume_.load(); }

    void SetLoop(bool loop);
    bool Loop() const { return loop_.load(); }

    // Fills per-channel VU levels (0..1). Returns number of channels written.
    int ChannelLevels(float* out, int maxCount);

    bool DeviceOk() const { return deviceOk_.load(); }
    int SampleRate() const { return sampleRate_.load(); }

    void NotifyDefaultDeviceChanged();

private:
    void ThreadProc();
    bool RunDevice();  // returns when device must be re-opened or on quit
    void Render(float* out, UINT32 frames);

    std::mutex mtx_;
    std::unique_ptr<openmpt::module> mod_;
    std::atomic<bool> hasModule_{false};
    std::atomic<bool> paused_{true};
    std::atomic<bool> loop_{false};
    std::atomic<float> volume_{0.8f};
    std::atomic<bool> deviceOk_{false};
    std::atomic<int> sampleRate_{48000};
    std::atomic<bool> quit_{false};
    std::atomic<bool> reopen_{false};
    bool endPosted_ = false;

    HWND notifyWnd_ = nullptr;
    UINT endMsg_ = 0;
    HANDLE wakeEvent_ = nullptr;
    std::thread thread_;
    struct DeviceNotifier* notifier_ = nullptr;
    struct IMMDeviceEnumerator* enumerator_ = nullptr;
};
