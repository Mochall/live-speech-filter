// Test player: plays a 48 kHz stereo 16-bit WAV on the default output device in Windows raw
// mode (device sound enhancements such as Nahimic bypassed), so a process-loopback capture of
// this program receives the file's samples unchanged. Used by tools/lite-playback-record-test.py.
//   lsf-play-test <file.wav>

#include "audio-io.hpp"

#include <mmdeviceapi.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

using namespace lsf;

static bool ReadWav(const wchar_t *path, std::vector<float> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char riff[12];
    in.read(riff, 12);
    if (!in || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) return false;
    bool fmt_ok = false;
    for (;;) {
        char id[4];
        uint32_t size;
        in.read(id, 4);
        in.read((char *)&size, 4);
        if (!in) return false;
        if (!memcmp(id, "fmt ", 4)) {
            std::vector<char> f(size);
            in.read(f.data(), size);
            uint16_t tag, ch, bits;
            uint32_t rate;
            memcpy(&tag, &f[0], 2);
            memcpy(&ch, &f[2], 2);
            memcpy(&rate, &f[4], 4);
            memcpy(&bits, &f[14], 2);
            fmt_ok = tag == 1 && ch == 2 && rate == 48000 && bits == 16;
            if (!fmt_ok) return false;
        } else if (!memcmp(id, "data", 4)) {
            if (!fmt_ok) return false;
            std::vector<int16_t> pcm(size / 2);
            in.read((char *)pcm.data(), (std::streamsize)pcm.size() * 2);
            out.resize(pcm.size());
            for (size_t i = 0; i < pcm.size(); i++) out[i] = pcm[i] / 32768.0f;
            return true;
        } else {
            in.seekg(size + (size & 1), std::ios::cur);
        }
    }
}

int wmain(int argc, wchar_t **argv) {
    bool raw = true; // --normal: ordinary stream, enhancements applied (for comparison)
    if (argc == 3 && !wcscmp(argv[1], L"--normal")) {
        raw = false;
        argv++;
        argc--;
    }
    if (argc != 2) {
        fprintf(stderr, "lsf-play-test [--normal] <48 kHz stereo 16-bit wav>\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<float> samples;
    if (!ReadWav(argv[1], samples)) {
        fprintf(stderr, "cannot read %ls (needs 48 kHz stereo 16-bit PCM)\n", argv[1]);
        return 1;
    }
    std::wstring id;
    for (auto &e : ListEndpoints(0))
        if (e.is_default) id = e.id;
    RenderStream render;
    std::string err;
    std::atomic<size_t> pos{0};
    const size_t total = samples.size() / 2;
    if (!render.OpenEndpoint(id, err, raw) ||
        !render.Start(
            [&](float *o, uint32_t n) {
                size_t p = pos.load();
                for (uint32_t i = 0; i < n; i++, p++) {
                    o[i * 2] = p < total ? samples[p * 2] : 0.0f;
                    o[i * 2 + 1] = p < total ? samples[p * 2 + 1] : 0.0f;
                }
                pos = p;
            },
            err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    while (pos.load() < total + 24000 && !render.failed) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    render.Stop();
    if (render.failed) {
        fprintf(stderr, "%s\n", render.failure.c_str());
        return 1;
    }
    CoUninitialize();
    return 0;
}
