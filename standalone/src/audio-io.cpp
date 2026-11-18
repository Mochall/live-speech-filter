#include "audio-io.hpp"

#include <mmdeviceapi.h>
#include <audioclientactivationparams.h>
#include <functiondiscoverykeys_devpkey.h>
#include <tlhelp32.h>
#include <avrt.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <cstdio>
#include <cstring>
#include <map>

using Microsoft::WRL::ComPtr;

namespace lsf {

std::string Utf8(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string HrText(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)hr);
    return buf;
}

uint64_t QpcNow100ns() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // Split to avoid overflow: whole seconds plus remainder.
    uint64_t sec = (uint64_t)now.QuadPart / (uint64_t)freq.QuadPart;
    uint64_t rem = (uint64_t)now.QuadPart % (uint64_t)freq.QuadPart;
    return sec * 10000000ULL + rem * 10000000ULL / (uint64_t)freq.QuadPart;
}

std::vector<Endpoint> ListEndpoints(int flow, bool all_states) {
    std::vector<Endpoint> out;
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return out;
    EDataFlow df = flow == 0 ? eRender : eCapture;
    std::wstring default_id;
    ComPtr<IMMDevice> def;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(df, eConsole, &def))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id))) {
            default_id = id;
            CoTaskMemFree(id);
        }
    }
    ComPtr<IMMDeviceCollection> devices;
    DWORD mask = all_states ? DEVICE_STATEMASK_ALL : DEVICE_STATE_ACTIVE;
    if (FAILED(en->EnumAudioEndpoints(df, mask, &devices))) return out;
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; i++) {
        ComPtr<IMMDevice> dev;
        if (FAILED(devices->Item(i, &dev))) continue;
        Endpoint e;
        LPWSTR id = nullptr;
        if (FAILED(dev->GetId(&id))) continue;
        e.id = id;
        CoTaskMemFree(id);
        dev->GetState(&e.state);
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) e.name = Utf8(v.pwszVal);
            PropVariantClear(&v);
        }
        e.is_default = e.id == default_id;
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<ProcessEntry> ListProcesses() {
    std::vector<ProcessEntry> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            out.push_back({pe.th32ProcessID, pe.th32ParentProcessID, Utf8(pe.szExeFile)});
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

DWORD FindProcessRoot(const std::string &exe_name) {
    auto procs = ListProcesses();
    std::map<DWORD, const ProcessEntry *> by_pid;
    for (auto &p : procs) by_pid[p.pid] = &p;
    auto same = [&](const std::string &a) { return _stricmp(a.c_str(), exe_name.c_str()) == 0; };
    for (auto &p : procs) {
        if (!same(p.exe)) continue;
        auto parent = by_pid.find(p.parent);
        if (parent == by_pid.end() || !same(parent->second->exe)) return p.pid;
    }
    return 0;
}

// ---------------------------------------------------------------------------------------

static WAVEFORMATEX FloatFormat() {
    WAVEFORMATEX f = {};
    f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    f.nChannels = kChannels;
    f.nSamplesPerSec = kRate;
    f.wBitsPerSample = 32;
    f.nBlockAlign = f.nChannels * f.wBitsPerSample / 8;
    f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
    return f;
}

static WAVEFORMATEX Pcm16Format() {
    WAVEFORMATEX f = FloatFormat();
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.wBitsPerSample = 16;
    f.nBlockAlign = f.nChannels * f.wBitsPerSample / 8;
    f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
    return f;
}

namespace {
class ActivateHandler : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                                            Microsoft::WRL::FtmBase,
                                                            IActivateAudioInterfaceCompletionHandler> {
public:
    ActivateHandler() { done = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~ActivateHandler() override {
        if (client) client->Release();
        CloseHandle(done);
    }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *op) override {
        IUnknown *unk = nullptr;
        HRESULT activate_hr = E_FAIL;
        result = op->GetActivateResult(&activate_hr, &unk);
        if (SUCCEEDED(result)) result = activate_hr;
        if (SUCCEEDED(result) && unk) result = unk->QueryInterface(IID_PPV_ARGS(&client));
        if (unk) unk->Release();
        SetEvent(done);
        return S_OK;
    }
    HANDLE done = nullptr;
    HRESULT result = E_FAIL;
    IAudioClient *client = nullptr;
};
} // namespace

CaptureStream::~CaptureStream() {
    Stop();
    if (capture_) capture_->Release();
    if (client_) client_->Release();
    if (event_) CloseHandle(event_);
}

bool CaptureStream::Initialize(DWORD flags, std::string &error) {
    WAVEFORMATEX fmt = FloatFormat();
    const REFERENCE_TIME buffer = 1000000; // 100 ms
    HRESULT hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, buffer, 0, &fmt, nullptr);
    if (FAILED(hr) && loopback_) {
        fmt = Pcm16Format();
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, buffer, 0, &fmt, nullptr);
        pcm16_ = SUCCEEDED(hr);
    }
    if (FAILED(hr)) {
        error = "IAudioClient::Initialize " + HrText(hr);
        return false;
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hr = client_->SetEventHandle(event_);
    if (SUCCEEDED(hr)) hr = client_->GetService(IID_PPV_ARGS(&capture_));
    if (FAILED(hr)) {
        error = "capture service " + HrText(hr);
        return false;
    }
    return true;
}

bool CaptureStream::OpenEndpoint(const std::wstring &id, std::string &error) {
    ComPtr<IMMDeviceEnumerator> en;
    ComPtr<IMMDevice> dev;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (SUCCEEDED(hr)) hr = en->GetDevice(id.c_str(), &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client_);
    if (FAILED(hr)) {
        error = "open capture device " + HrText(hr);
        return false;
    }
    loopback_ = false;
    return Initialize(AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                          AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                      error);
}

bool CaptureStream::OpenProcess(DWORD pid, std::string &error) {
    AUDIOCLIENT_ACTIVATION_PARAMS params = {};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv = {};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = (BYTE *)&params;
    auto handler = Microsoft::WRL::Make<ActivateHandler>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv,
                                             handler.Get(), &op);
    if (FAILED(hr)) {
        error = "process loopback activation " + HrText(hr) + " (needs Windows 10 2004 or newer)";
        return false;
    }
    if (WaitForSingleObject(handler->done, 5000) != WAIT_OBJECT_0) {
        error = "process loopback activation timed out";
        return false;
    }
    if (FAILED(handler->result) || !handler->client) {
        error = "process loopback activation " + HrText(handler->result);
        return false;
    }
    client_ = handler->client;
    handler->client = nullptr;
    loopback_ = true;
    return Initialize(AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                          AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                      error);
}

bool CaptureStream::Start(Callback callback, std::string &error) {
    callback_ = std::move(callback);
    HRESULT hr = client_->Start();
    if (FAILED(hr)) {
        error = "IAudioClient::Start " + HrText(hr);
        return false;
    }
    running_ = true;
    thread_ = std::thread(&CaptureStream::Run, this);
    return true;
}

void CaptureStream::Stop() {
    if (!running_ && !thread_.joinable()) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (client_) client_->Stop();
}

void CaptureStream::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task_index = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    std::vector<float> converted;
    std::vector<float> zeros;
    while (running_) {
        DWORD w = WaitForSingleObject(event_, 10);
        if (w == WAIT_TIMEOUT) {
            callback_(nullptr, 0, QpcNow100ns(), false);
            continue;
        }
        UINT32 next = 0;
        HRESULT hr = capture_->GetNextPacketSize(&next);
        while (SUCCEEDED(hr) && next > 0) {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 dev_pos = 0, qpc = 0;
            hr = capture_->GetBuffer(&data, &frames, &flags, &dev_pos, &qpc);
            if (FAILED(hr)) break;
            if (frames) {
                const float *samples;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    zeros.assign((size_t)frames * kChannels, 0.0f);
                    samples = zeros.data();
                } else if (pcm16_) {
                    converted.resize((size_t)frames * kChannels);
                    const int16_t *in = (const int16_t *)data;
                    for (size_t i = 0; i < converted.size(); i++) converted[i] = in[i] / 32768.0f;
                    samples = converted.data();
                } else {
                    samples = (const float *)data;
                }
                if (!qpc) qpc = QpcNow100ns() - (uint64_t)frames * 10000000ULL / kRate;
                callback_(samples, frames, qpc, (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0);
            }
            capture_->ReleaseBuffer(frames);
            hr = capture_->GetNextPacketSize(&next);
        }
        if (FAILED(hr)) {
            failure = "capture stopped " + HrText(hr);
            failed = true;
            break;
        }
    }
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

// ---------------------------------------------------------------------------------------

RenderStream::~RenderStream() {
    Stop();
    if (render_) render_->Release();
    if (client_) client_->Release();
    if (event_) CloseHandle(event_);
}

bool RenderStream::OpenEndpoint(const std::wstring &id, std::string &error, bool raw) {
    ComPtr<IMMDeviceEnumerator> en;
    ComPtr<IMMDevice> dev;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (SUCCEEDED(hr)) hr = en->GetDevice(id.c_str(), &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client_);
    if (FAILED(hr)) {
        error = "open output device " + HrText(hr);
        return false;
    }
    if (raw) {
        ComPtr<IAudioClient2> client2;
        if (SUCCEEDED(client_->QueryInterface(IID_PPV_ARGS(&client2)))) {
            AudioClientProperties props = {};
            props.cbSize = sizeof(props);
            props.eCategory = AudioCategory_Other;
            props.Options = AUDCLNT_STREAMOPTIONS_RAW;
            HRESULT rhr = client2->SetClientProperties(&props);
            if (FAILED(rhr)) {
                error = "raw mode not supported " + HrText(rhr);
                return false;
            }
        }
    }
    WAVEFORMATEX fmt = FloatFormat();
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                 AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                             500000 /* 50 ms */, 0, &fmt, nullptr);
    if (FAILED(hr)) {
        error = "output IAudioClient::Initialize " + HrText(hr);
        return false;
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hr = client_->SetEventHandle(event_);
    if (SUCCEEDED(hr)) hr = client_->GetBufferSize(&buffer_frames_);
    if (SUCCEEDED(hr)) hr = client_->GetService(IID_PPV_ARGS(&render_));
    if (FAILED(hr)) {
        error = "render service " + HrText(hr);
        return false;
    }
    return true;
}

bool RenderStream::Start(Pull pull, std::string &error) {
    pull_ = std::move(pull);
    BYTE *data = nullptr;
    // Start with a silent buffer.
    if (SUCCEEDED(render_->GetBuffer(buffer_frames_, &data))) render_->ReleaseBuffer(buffer_frames_, AUDCLNT_BUFFERFLAGS_SILENT);
    HRESULT hr = client_->Start();
    if (FAILED(hr)) {
        error = "output IAudioClient::Start " + HrText(hr);
        return false;
    }
    running_ = true;
    thread_ = std::thread(&RenderStream::Run, this);
    return true;
}

void RenderStream::Stop() {
    if (!running_ && !thread_.joinable()) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (client_) client_->Stop();
}

void RenderStream::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task_index = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    while (running_) {
        if (WaitForSingleObject(event_, 200) != WAIT_OBJECT_0) continue;
        UINT32 padding = 0;
        HRESULT hr = client_->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            failure = "output stopped " + HrText(hr);
            failed = true;
            break;
        }
        UINT32 avail = buffer_frames_ - padding;
        if (!avail) continue;
        BYTE *data = nullptr;
        hr = render_->GetBuffer(avail, &data);
        if (FAILED(hr)) {
            failure = "output stopped " + HrText(hr);
            failed = true;
            break;
        }
        pull_((float *)data, avail);
        render_->ReleaseBuffer(avail, 0);
    }
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

} // namespace lsf
