// Command-line prototype of the standalone filter (no OBS): microphone and selected
// programs -> recognition and muting -> VB-CABLE Input. See standalone/README.md.

#include "app-core.hpp"
#include "censor-log.hpp"
#include "companion-audio.hpp"

#include <mmdeviceapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace lsf;
namespace fs = std::filesystem;

static std::atomic<bool> g_stop{false};
static FILE *g_log = nullptr;
static std::mutex g_log_mutex;

static void LogLine(int level, const char *message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    SYSTEMTIME t;
    GetLocalTime(&t);
    const char *tag = level <= censor::kLogError ? "error" : level <= censor::kLogWarning ? "warning" : "info";
    fprintf(stderr, "%02d:%02d:%02d.%03d [%s] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, tag, message);
    if (g_log) {
        fprintf(g_log, "%04d-%02d-%02d %02d:%02d:%02d.%03d [%s] %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                t.wSecond, t.wMilliseconds, tag, message);
        fflush(g_log);
    }
}

static BOOL WINAPI OnCtrl(DWORD) {
    g_stop = true;
    return TRUE;
}

static fs::path ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

static bool Contains(const std::string &hay, const std::string &needle) {
    return hay.find(needle) != std::string::npos;
}

static void Usage() {
    fprintf(stderr,
            "lsf-lite-cli --list\n"
            "lsf-lite-cli [--mic <name part>|default|none] [--app <exe name>]... [--out <name part>|none]\n"
            "             [--model <dir>] [--data <dir>] [--custom <file>] [--pinyin <file>]\n"
            "             [--padding <ms>] [--delay <s>] [--seconds <n>] [--log <file>] [--debug]\n"
            "             [--delay-test <s>]  (test only: delay without the minimum, to measure how low it can go)\n"
            "             [--lanes <n>] [--lane-step <ms>]  (parallel recognizer streams, chunk offset)\n"
            "             [--chunk <ms>]  (audio handed to the recognizer per step, default 40)\n"
            "             [--effect <n>]  (0 beep, 1 silence (default), 3 telegraph)\n"
            "             [--clip <48 kHz mono 16-bit wav> [--clip-gap <ms>]]  (played at each mute, repeated)\n"
            "             [--dump <dir>]  (raw captured audio per source, 48 kHz stereo f32le)\n"
            "lsf-lite-cli --file <in.wav> --write <out.wav> [options]  (file test at real-time pace, no devices)\n"
            "Defaults: --mic default, --out \"CABLE Input\", data and model next to the program.\n");
}

static double Db(float peak) { return peak > 0 ? 20.0 * std::log10(peak) : -120.0; }

int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool unthrottled = DisablePowerThrottling();
    censor::SetLogHandler(LogLine);

    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) args.push_back(Utf8(argv[i]));
    std::string mic = "default", out_name = "CABLE Input";
    std::vector<std::string> apps;
    fs::path data_dir = ExeDir() / "data";
    fs::path model_dir;
    fs::path custom_file, pinyin_file, log_file, dump_dir, file_in, write_out;
    int padding_ms = 500;
    double delay = -1, seconds = 0;
    bool delay_unclamped = false;
    bool debug = false, list = false;
    int lanes = AppSettings{}.recognizer_lanes, lane_step_ms = AppSettings{}.lane_step_ms;
    int chunk_ms = AppSettings{}.feed_chunk_ms;
    int effect = 1, clip_gap_ms = 0;
    fs::path clip_file;
    for (size_t i = 0; i < args.size(); i++) {
        auto next = [&](std::string &v) {
            if (i + 1 >= args.size()) {
                Usage();
                exit(2);
            }
            v = args[++i];
        };
        std::string v;
        const std::string &a = args[i];
        if (a == "--list") list = true;
        else if (a == "--mic") next(mic);
        else if (a == "--app") { next(v); apps.push_back(v); }
        else if (a == "--out") next(out_name);
        else if (a == "--model") { next(v); model_dir = fs::path(Wide(v)); }
        else if (a == "--data") { next(v); data_dir = fs::path(Wide(v)); }
        else if (a == "--custom") { next(v); custom_file = fs::path(Wide(v)); }
        else if (a == "--pinyin") { next(v); pinyin_file = fs::path(Wide(v)); }
        else if (a == "--log") { next(v); log_file = fs::path(Wide(v)); }
        else if (a == "--dump") { next(v); dump_dir = fs::path(Wide(v)); }
        else if (a == "--file") { next(v); file_in = fs::path(Wide(v)); }
        else if (a == "--write") { next(v); write_out = fs::path(Wide(v)); }
        else if (a == "--padding") { next(v); padding_ms = std::stoi(v); }
        else if (a == "--delay") { next(v); delay = std::stod(v); }
        else if (a == "--delay-test") { next(v); delay = std::stod(v); delay_unclamped = true; }
        else if (a == "--seconds") { next(v); seconds = std::stod(v); }
        else if (a == "--debug") debug = true;
        else if (a == "--lanes") { next(v); lanes = std::stoi(v); }
        else if (a == "--lane-step") { next(v); lane_step_ms = std::stoi(v); }
        else if (a == "--chunk") { next(v); chunk_ms = std::stoi(v); }
        else if (a == "--effect") { next(v); effect = std::stoi(v); }
        else if (a == "--clip") { next(v); clip_file = fs::path(Wide(v)); }
        else if (a == "--clip-gap") { next(v); clip_gap_ms = std::stoi(v); }
        else { Usage(); return 2; }
    }

    if (list) {
        printf("Capture devices:\n");
        for (auto &e : ListEndpoints(1)) printf("  %s%s\n", e.name.c_str(), e.is_default ? "  (default)" : "");
        printf("Output devices:\n");
        for (auto &e : ListEndpoints(0, true))
            printf("  %s%s%s\n", e.name.c_str(), e.is_default ? "  (default)" : "",
                   e.state == DEVICE_STATE_ACTIVE ? "" : e.state == DEVICE_STATE_DISABLED ? "  [disabled]" : "  [not active]");
        return 0;
    }

    if (!log_file.empty()) g_log = _wfopen(log_file.c_str(), L"ab");
    CLOG(unthrottled ? censor::kLogInfo : censor::kLogWarning, "[Lite] Windows power throttling %s",
         unthrottled ? "off" : "could not be turned off");

    if (model_dir.empty()) {
        std::error_code ec;
        for (auto &d : fs::directory_iterator(ExeDir() / "models", ec)) {
            if (d.is_directory() && fs::exists(d.path() / "tokens.txt")) {
                model_dir = d.path();
                break;
            }
        }
    }
    AppSettings s;
    s.model_path = Utf8(model_dir.wstring());
    s.censor_padding_ms = padding_ms;
    double min_delay = padding_ms / 1000.0 + AppSettings::kMinDelayMarginMs / 1000.0;
    s.delay_seconds = delay < 0 ? padding_ms / 1000.0 + AppSettings::kAutoDelayMarginMs / 1000.0
                      : delay_unclamped ? delay : std::max(delay, min_delay);
    s.dict_dir = data_dir / "dict";
    s.builtin_words = ReadTextFile(data_dir / "builtin_dirty_words.txt");
    if (!custom_file.empty()) s.custom_words = ReadTextFile(custom_file);
    if (!pinyin_file.empty()) s.pinyin_words = ReadTextFile(pinyin_file);
    s.debug_logging = debug;
    s.recognizer_lanes = lanes;
    s.lane_step_ms = lane_step_ms;
    s.feed_chunk_ms = chunk_ms;
    s.effect = effect;
    if (!clip_file.empty()) {
        // 48 kHz mono 16-bit PCM with a plain 44-byte header, as in standalone/data/sounds.
        const std::string bytes = ReadTextFile(clip_file);
        if (bytes.size() <= 44 || bytes.compare(0, 4, "RIFF") != 0) {
            fprintf(stderr, "cannot read clip %s\n", Utf8(clip_file.wstring()).c_str());
            return 2;
        }
        auto clip = std::make_shared<std::vector<float>>((bytes.size() - 44) / 2);
        for (size_t i = 0; i < clip->size(); i++) {
            int16_t v;
            std::memcpy(&v, bytes.data() + 44 + i * 2, 2);
            (*clip)[i] = v / 32768.0f;
        }
        s.effect = 4;
        s.clip = clip;
        s.clip_period = clip->size() + (size_t)clip_gap_ms * 48;
    }
    if (s.model_path.empty()) {
        CLOG(censor::kLogError, "[Lite] no model folder (use --model)");
        return 1;
    }
    if (s.builtin_words.empty()) CLOG(censor::kLogWarning, "[Lite] built-in word list not found in %s", Utf8(data_dir.wstring()).c_str());
    SharedSettings settings;
    settings.Set(s);
    CLOG(censor::kLogInfo, "[Lite] model %s, delay %.2f s, padding %d ms, %d recognizer stream(s) %d ms apart",
         s.model_path.c_str(), s.delay_seconds, padding_ms, lanes, lane_step_ms);

    if (!file_in.empty()) {
        // File test: feed the WAV at real-time pace (the recognizer thread sees the same timing as
        // with a device) and write the filtered output, no devices involved.
        std::vector<float> in;
        if (!ReadWav48(file_in, in)) {
            CLOG(censor::kLogError, "[Lite] cannot read %s (48 kHz stereo 16-bit WAV)", Utf8(file_in.wstring()).c_str());
            return 1;
        }
        Source src(Source::Kind::Device, "file", &settings);
        src.StartFileInput();
        for (int i = 0; i < 300 && src.engine.loaded_model_path.empty(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::lock_guard<std::mutex> lock(src.engine.history_mutex);
            if (!src.engine.initialization_error.empty()) break;
        }
        Mixer mixer;
        mixer.SetSources({&src});
        const uint32_t block = 480;
        const size_t in_frames = in.size() / kChannels;
        const size_t total = in_frames + (size_t)((s.delay_seconds + 1.0) * kRate);
        std::vector<float> out(total * kChannels), zeros(block * kChannels, 0.0f);
        auto start = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos + block <= total && !g_stop; pos += block) {
            std::this_thread::sleep_until(start + std::chrono::microseconds((long long)(pos * 1000000ULL / kRate)));
            src.FeedFrames(pos + block <= in_frames ? &in[pos * kChannels] : zeros.data(), block);
            mixer.Pull(&out[pos * kChannels], block);
        }
        mixer.SetSources({});
        CLOG(censor::kLogInfo, "[Lite] file done: muted %zu, late %zu", src.engine.censored_count.load(),
             src.engine.late_beeps_count.load());
        src.Stop();
        src.engine.Stop();
        if (!WriteWav48(write_out, out)) {
            CLOG(censor::kLogError, "[Lite] cannot write %s", Utf8(write_out.wstring()).c_str());
            return 1;
        }
        if (g_log) fclose(g_log);
        return 0;
    }

    // Output device ("none": capture only, e.g. to record a program's sound with --dump)
    const bool no_output = out_name == "none";
    const Endpoint *out_ep = nullptr;
    auto renders = ListEndpoints(0, true);
    for (auto &e : renders)
        if (!no_output) // an enabled device first, a disabled one only to report it
        // "CABLE Input" also finds the cable's stereo endpoint under another name ("扬声器 (VB-Audio
        // Virtual Cable)" after a fresh install on Chinese Windows).
        if ((Contains(e.name, out_name) ||
             (out_name == "CABLE Input" && IsCableEndpointName(e.name) && !IsCable16chEndpointName(e.name))) &&
            (!out_ep || (out_ep->state != DEVICE_STATE_ACTIVE && e.state == DEVICE_STATE_ACTIVE)))
            out_ep = &e;
    if (!out_ep && !no_output) {
        CLOG(censor::kLogError, "[Lite] output device \"%s\" not found", out_name.c_str());
        return 1;
    }
    if (out_ep && out_ep->state != DEVICE_STATE_ACTIVE) {
        CLOG(censor::kLogError, "[Lite] output device \"%s\" is disabled in Windows sound settings", out_ep->name.c_str());
        return 1;
    }
    for (auto &e : ListEndpoints(0))
        if (out_ep && e.is_default && e.id == out_ep->id)
            CLOG(censor::kLogWarning, "[Lite] \"%s\" is the Windows default playback device: every program's sound goes into the broadcast unfiltered", e.name.c_str());

    std::vector<std::unique_ptr<Source>> sources;
    if (mic != "none") {
        const Endpoint *mic_ep = nullptr;
        auto caps = ListEndpoints(1);
        for (auto &e : caps)
            if (mic == "default" ? e.is_default : Contains(e.name, mic)) { mic_ep = &e; break; }
        if (!mic_ep) {
            CLOG(censor::kLogError, "[Lite] microphone \"%s\" not found", mic.c_str());
            return 1;
        }
        if (Contains(mic_ep->name, "CABLE")) {
            CLOG(censor::kLogError, "[Lite] \"%s\" is the VB-CABLE side, not a microphone", mic_ep->name.c_str());
            return 1;
        }
        auto src = std::make_unique<Source>(Source::Kind::Device, "麦克风: " + mic_ep->name, &settings);
        if (!dump_dir.empty()) src->SetDumpFile(dump_dir / L"mic.f32");
        std::string err;
        if (!src->OpenDevice(mic_ep->id, err)) {
            CLOG(censor::kLogError, "[Lite] microphone: %s", err.c_str());
            return 1;
        }
        CLOG(censor::kLogInfo, "[Lite] capturing microphone %s", mic_ep->name.c_str());
        sources.push_back(std::move(src));
    }
    for (auto &app : apps) {
        auto src = std::make_unique<Source>(Source::Kind::Process, app, &settings);
        src->SetProcessName(app);
        if (!dump_dir.empty()) src->SetDumpFile(dump_dir / (Wide(app) + L".f32"));
        src->MaintainProcess();
        sources.push_back(std::move(src));
    }
    if (sources.empty()) {
        CLOG(censor::kLogError, "[Lite] nothing to capture");
        return 1;
    }

    Mixer mixer;
    std::vector<Source *> raw;
    for (auto &p : sources) raw.push_back(p.get());
    mixer.SetSources(raw);
    RenderStream render;
    std::string err;
    if (out_ep) {
        if (!render.OpenEndpoint(out_ep->id, err) || !render.Start([&](float *o, uint32_t n) { mixer.Pull(o, n); }, err)) {
            CLOG(censor::kLogError, "[Lite] output: %s", err.c_str());
            return 1;
        }
        CLOG(censor::kLogInfo, "[Lite] output to %s", out_ep->name.c_str());
    } else {
        CLOG(censor::kLogInfo, "[Lite] no output (capture only)");
    }

    SetConsoleCtrlHandler(OnCtrl, TRUE);
    auto start = std::chrono::steady_clock::now();
    auto last_stats = start;
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        for (auto &src : sources) src->MaintainProcess();
        if (out_ep && render.failed) {
            CLOG(censor::kLogError, "[Lite] %s", render.failure.c_str());
            break;
        }
        auto now = std::chrono::steady_clock::now();
        if (seconds > 0 && std::chrono::duration<double>(now - start).count() >= seconds) break;
        if (now - last_stats >= std::chrono::seconds(5)) {
            last_stats = now;
            for (auto &src : sources) {
                CensorEngine &e = src->engine;
                std::string state;
                {
                    std::lock_guard<std::mutex> lock(e.history_mutex);
                    if (e.is_loading) state = "loading";
                    else if (!e.initialization_error.empty()) state = "ERROR " + e.initialization_error;
                    else if (e.loaded_model_path.empty()) state = "waiting";
                    else state = "ok";
                }
                size_t queued;
                {
                    std::lock_guard<std::mutex> lock(e.queue_mutex);
                    queued = e.asr_queue.size();
                }
                bool attached = src->kind() == Source::Kind::Device || src->attached();
                CLOG(censor::kLogInfo,
                     "[Stats] %s: %s, model %s, in %.0f dB, muted %zu, late %zu, backlog %.2f s, fifo %.0f ms, "
                     "ratio %.5f, underruns %llu, silence filled %.1f s, dropped %llu",
                     src->name().c_str(), attached ? "attached" : ("not attached: " + src->capture_error()).c_str(),
                     state.c_str(), Db(src->input_peak.exchange(0)), e.censored_count.load(), e.late_beeps_count.load(),
                     queued / 16000.0, src->fifo_ms.load(), src->ratio.load(), (unsigned long long)src->underruns.load(),
                     src->inserted_silence_frames.load() / (double)kRate, (unsigned long long)src->dropped_frames.load());
            }
            CLOG(censor::kLogInfo, "[Stats] output peak %.0f dB", Db(mixer.output_peak.exchange(0)));
        }
    }
    CLOG(censor::kLogInfo, "[Lite] stopping");
    if (out_ep) render.Stop();
    mixer.SetSources({});
    sources.clear();
    if (g_log) fclose(g_log);
    CoUninitialize();
    return 0;
}
