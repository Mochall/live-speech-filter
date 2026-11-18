#pragma once

// WASAPI capture (microphone endpoint or one process tree via process loopback) and render.
// All streams run in shared mode as 48 kHz stereo 32-bit float; Windows converts from the
// device format (AUTOCONVERTPCM).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace lsf {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kChannels = 2;

std::string Utf8(const std::wstring &w);
std::wstring Wide(const std::string &s);
std::string HrText(HRESULT hr);

// Current time in 100 ns units on the QueryPerformanceCounter clock (same clock as the
// WASAPI capture positions).
uint64_t QpcNow100ns();

struct Endpoint {
    std::wstring id;
    std::string name; // UTF-8 friendly name
    bool is_default = false;
    DWORD state = 0;  // DEVICE_STATE_*
};

// flow: 0 = render, 1 = capture. Only active endpoints unless all_states.
std::vector<Endpoint> ListEndpoints(int flow, bool all_states = false);

struct ProcessEntry {
    DWORD pid;
    DWORD parent;
    std::string exe; // UTF-8, as in the snapshot (e.g. "KOOK.exe")
};
std::vector<ProcessEntry> ListProcesses();
// Root of the process tree for an executable name: an instance whose parent is not the
// same executable. 0 if none is running.
DWORD FindProcessRoot(const std::string &exe_name);

class CaptureStream {
public:
    // frames == 0: idle tick (no packet within ~10 ms); qpc is the current time then.
    // Otherwise qpc is the capture time of the first frame (100 ns units).
    using Callback = std::function<void(const float *interleaved, uint32_t frames, uint64_t qpc, bool discontinuity)>;

    ~CaptureStream();
    bool OpenEndpoint(const std::wstring &id, std::string &error);
    // Captures what the process and its child processes play (Windows 10 2004 or newer).
    bool OpenProcess(DWORD pid, std::string &error);
    bool Start(Callback callback, std::string &error);
    void Stop();
    bool running() const { return running_; }
    // Set when the stream stopped by itself (device removed, error).
    std::atomic<bool> failed{false};
    std::string failure;

private:
    void Run();
    bool Initialize(DWORD flags, std::string &error);

    IAudioClient *client_ = nullptr;
    IAudioCaptureClient *capture_ = nullptr;
    HANDLE event_ = nullptr;
    bool loopback_ = false;
    bool pcm16_ = false; // process loopback fell back to 16-bit PCM
    std::thread thread_;
    std::atomic<bool> running_{false};
    Callback callback_;
};

class RenderStream {
public:
    // Fill out with frames interleaved stereo float frames.
    using Pull = std::function<void(float *out, uint32_t frames)>;

    ~RenderStream();
    // raw: ask Windows to bypass the device's sound enhancements (if the device supports it).
    bool OpenEndpoint(const std::wstring &id, std::string &error, bool raw = false);
    bool Start(Pull pull, std::string &error);
    void Stop();
    std::atomic<bool> failed{false};
    std::string failure;

private:
    void Run();

    IAudioClient *client_ = nullptr;
    IAudioRenderClient *render_ = nullptr;
    HANDLE event_ = nullptr;
    uint32_t buffer_frames_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{false};
    Pull pull_;
};

} // namespace lsf
