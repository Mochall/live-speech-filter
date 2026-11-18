#include "lite-checks.hpp"
#include "audio-io.hpp"

#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>

#include <algorithm>
#include <map>

using Microsoft::WRL::ComPtr;

namespace lsf {

namespace {

std::string ExeOfPid(DWORD pid) {
    std::string exe;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &size)) {
            exe = Utf8(path);
            auto slash = exe.find_last_of("\\/");
            if (slash != std::string::npos) exe = exe.substr(slash + 1);
        }
        CloseHandle(h);
    }
    return exe;
}

// Calls f(pid, active) for every session of the device.
template <typename F> void ForEachSession(IMMDevice *device, F f) {
    ComPtr<IAudioSessionManager2> mgr;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void **)&mgr)) ||
        FAILED(mgr->GetSessionEnumerator(&sessions)))
        return;
    int n = 0;
    sessions->GetCount(&n);
    for (int i = 0; i < n; i++) {
        ComPtr<IAudioSessionControl> ctl;
        ComPtr<IAudioSessionControl2> ctl2;
        AudioSessionState state;
        DWORD pid = 0;
        if (FAILED(sessions->GetSession(i, &ctl)) || FAILED(ctl.As(&ctl2)) || FAILED(ctl2->GetState(&state)) ||
            FAILED(ctl2->GetProcessId(&pid)) || !pid || ctl2->IsSystemSoundsSession() == S_OK)
            continue;
        if (state == AudioSessionStateExpired) continue;
        f(pid, state == AudioSessionStateActive);
    }
}

} // namespace

std::vector<AudioProgram> AudioPrograms() {
    std::map<std::string, AudioProgram> found; // by lower-case exe
    ComPtr<IMMDeviceEnumerator> en;
    ComPtr<IMMDeviceCollection> list;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))) ||
        FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &list)))
        return {};
    UINT count = 0;
    list->GetCount(&count);
    const DWORD self = GetCurrentProcessId();
    for (UINT i = 0; i < count; i++) {
        ComPtr<IMMDevice> d;
        if (FAILED(list->Item(i, &d))) continue;
        ForEachSession(d.Get(), [&](DWORD pid, bool active) {
            if (pid == self) return;
            std::string exe = ExeOfPid(pid);
            if (exe.empty()) return;
            std::string key = exe;
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
            auto &p = found[key];
            if (p.exe.empty()) p = {pid, exe, active};
            p.active |= active;
        });
    }
    std::vector<AudioProgram> out;
    for (auto &kv : found) out.push_back(kv.second);
    return out;
}

std::vector<std::string> ProgramsPlayingTo(const std::wstring &endpoint_id) {
    std::vector<std::string> out;
    ComPtr<IMMDeviceEnumerator> en;
    ComPtr<IMMDevice> d;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))) ||
        FAILED(en->GetDevice(endpoint_id.c_str(), &d)))
        return out;
    const DWORD self = GetCurrentProcessId();
    ForEachSession(d.Get(), [&](DWORD pid, bool active) {
        if (pid == self || !active) return;
        std::string exe = ExeOfPid(pid);
        if (exe.empty()) exe = "进程 " + std::to_string(pid);
        if (std::find(out.begin(), out.end(), exe) == out.end()) out.push_back(exe);
    });
    return out;
}

} // namespace lsf
