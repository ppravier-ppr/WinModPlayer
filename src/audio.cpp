// audio.cpp - libopenmpt + WASAPI
#include "audio.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>

#include <libopenmpt/libopenmpt.hpp>

#include <cmath>
#include <cstring>
#include <map>
#include <ostream>

// libopenmpt keeps a reference to its log stream; a stream without buffer discards everything.
static std::ostream g_nullLog(nullptr);

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

bool ReadWholeFile(const std::wstring& path, std::vector<std::uint8_t>& out, size_t maxBytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && (unsigned long long)sz.QuadPart <= maxBytes;
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        DWORD rd = 0;
        ok = ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr) && rd == out.size();
    }
    CloseHandle(h);
    return ok;
}

// ---------------------------------------------------------------------------
// Default device change notifications -> re-open the stream on the new device
// ---------------------------------------------------------------------------
struct DeviceNotifier : public IMMNotificationClient {
    AudioEngine* engine;
    LONG ref = 1;
    explicit DeviceNotifier(AudioEngine* e) : engine(e) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&ref);
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) engine->NotifyDefaultDeviceChanged();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

AudioEngine::AudioEngine() {}
AudioEngine::~AudioEngine() { Stop(); }

bool AudioEngine::Start(HWND notifyWnd, UINT endMsg) {
    notifyWnd_ = notifyWnd;
    endMsg_ = endMsg;
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), (void**)&enumerator_))) {
        notifier_ = new DeviceNotifier(this);
        enumerator_->RegisterEndpointNotificationCallback(notifier_);
    }
    quit_ = false;
    thread_ = std::thread(&AudioEngine::ThreadProc, this);
    return true;
}

void AudioEngine::Stop() {
    if (thread_.joinable()) {
        quit_ = true;
        SetEvent(wakeEvent_);
        thread_.join();
    }
    if (enumerator_ && notifier_) enumerator_->UnregisterEndpointNotificationCallback(notifier_);
    SafeRelease(notifier_);
    SafeRelease(enumerator_);
    if (wakeEvent_) { CloseHandle(wakeEvent_); wakeEvent_ = nullptr; }
    std::lock_guard<std::mutex> lk(mtx_);
    mod_.reset();
    hasModule_ = false;
}

void AudioEngine::NotifyDefaultDeviceChanged() {
    reopen_ = true;
    if (wakeEvent_) SetEvent(wakeEvent_);
}

bool AudioEngine::Load(const std::wstring& path, ModuleInfo& info, std::wstring& error) {
    std::vector<std::uint8_t> data;
    if (!ReadWholeFile(path, data)) { error = L"Cannot read file"; return false; }

    std::unique_ptr<openmpt::module> m;
    try {
        std::map<std::string, std::string> ctls{{"play.at_end", "stop"}};
        m = std::make_unique<openmpt::module>(data.data(), data.size(), g_nullLog, ctls);
    } catch (const std::exception& e) {
        error = L"Unsupported or corrupt module (" + Utf8ToWide(e.what()) + L")";
        return false;
    } catch (...) {
        error = L"Unsupported or corrupt module";
        return false;
    }

    info = ModuleInfo{};
    info.title = Utf8ToWide(m->get_metadata("title"));
    info.type = Utf8ToWide(m->get_metadata("type"));
    info.typeLong = Utf8ToWide(m->get_metadata("type_long"));
    info.tracker = Utf8ToWide(m->get_metadata("tracker"));
    info.artist = Utf8ToWide(m->get_metadata("artist"));
    info.message = Utf8ToWide(m->get_metadata("message_raw"));
    if (info.message.empty()) info.message = Utf8ToWide(m->get_metadata("message"));
    info.channels = m->get_num_channels();
    info.patterns = m->get_num_patterns();
    info.orders = m->get_num_orders();
    info.instrumentsCount = m->get_num_instruments();
    info.samplesCount = m->get_num_samples();
    info.duration = m->get_duration_seconds();
    for (auto& s : m->get_instrument_names()) info.instruments.push_back(Utf8ToWide(s));
    for (auto& s : m->get_sample_names()) info.samples.push_back(Utf8ToWide(s));

    m->set_repeat_count(loop_ ? -1 : 0);

    std::unique_ptr<openmpt::module> old;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        old = std::move(mod_);
        mod_ = std::move(m);
        endPosted_ = false;
        hasModule_ = true;
    }
    return true;  // 'old' destroyed outside the lock
}

void AudioEngine::Unload() {
    std::unique_ptr<openmpt::module> old;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        old = std::move(mod_);
        hasModule_ = false;
    }
}

double AudioEngine::Position() {
    std::lock_guard<std::mutex> lk(mtx_);
    return mod_ ? mod_->get_position_seconds() : 0.0;
}

void AudioEngine::Seek(double seconds) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (mod_) {
        mod_->set_position_seconds(seconds < 0 ? 0 : seconds);
        endPosted_ = false;
    }
}

void AudioEngine::SetLoop(bool loop) {
    loop_ = loop;
    std::lock_guard<std::mutex> lk(mtx_);
    if (mod_) mod_->set_repeat_count(loop ? -1 : 0);
}

int AudioEngine::ChannelLevels(float* out, int maxCount) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!mod_) return 0;
    int n = mod_->get_num_channels();
    if (n > maxCount) n = maxCount;
    for (int i = 0; i < n; ++i) out[i] = mod_->get_current_channel_vu_mono(i);
    return n;
}

void AudioEngine::Render(float* out, UINT32 frames) {
    size_t done = 0;
    bool ended = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (mod_ && !paused_) {
            while (done < frames) {
                size_t n = mod_->read_interleaved_stereo(sampleRate_, frames - done, out + done * 2);
                if (n == 0) { ended = true; break; }
                done += n;
            }
            if (ended && !endPosted_) {
                endPosted_ = true;
                PostMessageW(notifyWnd_, endMsg_, 0, 0);
            }
        }
    }
    if (done < frames) std::memset(out + done * 2, 0, (frames - done) * 2 * sizeof(float));
    // perceptual volume curve
    float v = volume_.load();
    float g = v * v;
    if (g != 1.0f)
        for (size_t i = 0; i < done * 2; ++i) out[i] *= g;
}

void AudioEngine::ThreadProc() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE avrt = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
    while (!quit_) {
        reopen_ = false;
        if (!RunDevice() && !quit_) {
            deviceOk_ = false;
            WaitForSingleObject(wakeEvent_, 1000);  // no device: retry periodically
        }
    }
    if (avrt) AvRevertMmThreadCharacteristics(avrt);
    CoUninitialize();
}

bool AudioEngine::RunDevice() {
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* mix = nullptr;
    HANDLE ev = nullptr;
    bool ok = false;

    do {
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), (void**)&en))) break;
        if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) break;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client))) break;
        if (FAILED(client->GetMixFormat(&mix))) break;

        int rate = (int)mix->nSamplesPerSec;
        if (rate < 8000 || rate > 384000) rate = 48000;

        // We always render float stereo; WASAPI converts channel count if needed.
        WAVEFORMATEXTENSIBLE wf{};
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wf.Format.nChannels = 2;
        wf.Format.nSamplesPerSec = rate;
        wf.Format.wBitsPerSample = 32;
        wf.Format.nBlockAlign = 8;
        wf.Format.nAvgBytesPerSec = rate * 8;
        wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wf.Samples.wValidBitsPerSample = 32;
        wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wf.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

        const REFERENCE_TIME bufferDuration = 600000;  // 60 ms
        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                      AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufferDuration, 0,
                                      (WAVEFORMATEX*)&wf, nullptr))) break;

        ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (FAILED(client->SetEventHandle(ev))) break;
        UINT32 bufFrames = 0;
        if (FAILED(client->GetBufferSize(&bufFrames))) break;
        if (FAILED(client->GetService(__uuidof(IAudioRenderClient), (void**)&render))) break;

        sampleRate_ = rate;

        // prime with silence
        BYTE* data = nullptr;
        if (SUCCEEDED(render->GetBuffer(bufFrames, &data))) render->ReleaseBuffer(bufFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        if (FAILED(client->Start())) break;
        deviceOk_ = true;
        ok = true;

        HANDLE waits[2] = {ev, wakeEvent_};
        while (!quit_ && !reopen_) {
            WaitForMultipleObjects(2, waits, FALSE, 200);
            if (quit_ || reopen_) break;
            UINT32 padding = 0;
            if (FAILED(client->GetCurrentPadding(&padding))) break;  // device lost
            UINT32 avail = bufFrames - padding;
            if (!avail) continue;
            if (FAILED(render->GetBuffer(avail, &data))) break;
            Render(reinterpret_cast<float*>(data), avail);
            render->ReleaseBuffer(avail, 0);
        }
        client->Stop();
        if (!quit_ && !reopen_) { ok = false; }  // error -> wait a bit then retry
    } while (false);

    if (mix) CoTaskMemFree(mix);
    SafeRelease(render);
    SafeRelease(client);
    SafeRelease(dev);
    SafeRelease(en);
    if (ev) CloseHandle(ev);
    return ok;
}
