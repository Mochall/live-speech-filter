#pragma once

// One filtered source per microphone or program: capture -> continuous 48 kHz stereo
// timeline -> CensorEngine (delay + recognition + muting) -> FIFO. The mixer sums the
// FIFOs into the output device (VB-CABLE Input) and keeps each FIFO at a small target
// fill with a slight resampling ratio, so differing device clocks do not drift.

#include "audio-io.hpp"
#include "censor-engine.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lsf {

struct AppSettings {
    bool enable = true;
    std::string model_path;          // UTF-8
    double delay_seconds = 2.0;
    int censor_padding_ms = 500;
    int effect = 1;                  // CensorAudioSettings::effect: 0 beep, 1 silence, 3 telegraph, 4 clip
    std::shared_ptr<const std::vector<float>> clip; // effect 4: 48 kHz mono
    size_t clip_period = 0;          // effect 4: samples from one start of the clip to the next
    int model_offset_ms = 0;         // shifts every mute (negative: earlier)
    bool debug_logging = false;
    int recognizer_lanes = 4;        // parallel recognizer streams (CensorAsrSettings::recognizer_lanes); 4 x 40 ms: every chunk position 9/9 in the file test
    int lane_step_ms = 40;           // chunk position offset between them
    int feed_chunk_ms = 40;          // audio handed to the recognizer per step (fixed size: repeatable results); 40 ms: 1.0 s file test lead +0.26 s vs +0.23 s at 100 ms, 160 ms missed a word start
    std::filesystem::path dict_dir;
    std::string builtin_words;       // regex list shipped with the program
    std::string custom_words;        // user's plain words
    std::string pinyin_words;        // supplemental pinyin lexicon
    bool use_pinyin = true;

    // Delay = padding + recognition margin. The plugin keeps 1.5 s / 1.3 s; Lite uses 0.9 s (auto 1.4 s
    // at the standard padding): file and microphone tests 2026-11-14 covered every word down to 1.2 s,
    // 1.0 s let a word start through. The minimum lets a user go a little lower by ear.
    static constexpr int kAutoDelayMarginMs = 900;
    static constexpr int kMinDelayMarginMs = 700;
};

class SharedSettings {
public:
    void Set(const AppSettings &s);
    AppSettings Get();
    void GetLexicon(CensorLexicon &lexicon);

private:
    std::mutex mutex_;
    AppSettings settings_;
    std::vector<std::regex> patterns_;
    std::string word_list_;
};

class Source : public CensorHost {
public:
    enum class Kind { Device, Process };

    Source(Kind kind, std::string name, SharedSettings *settings);
    ~Source() override;

    // Device: endpoint id. Process: executable name (e.g. "KOOK.exe"), attached when running.
    bool OpenDevice(const std::wstring &endpoint_id, std::string &error);
    void SetProcessName(const std::string &exe) { exe_ = exe; }
    // Diagnostics: append the captured audio (48 kHz stereo f32le, as delivered) to a file.
    void SetDumpFile(const std::filesystem::path &path);
    // Process sources: attach/re-attach when the program (re)starts. Call periodically.
    void MaintainProcess();
    void Stop();
    // File test input (no device): start recognition, then feed captured-format audio.
    void StartFileInput() { engine.Start(); }
    void FeedFrames(const float *interleaved, uint32_t frames) { OnCapture(interleaved, frames, 0, false); }

    Kind kind() const { return kind_; }
    const std::string &name() const { return name_; }
    bool attached() const { return capture_ && capture_->running() && !capture_->failed; }
    std::string capture_error();

    // Mixer side (render thread).
    void MixInto(float *out, uint32_t frames);

    // CensorHost
    void GetAudioSettings(CensorAudioSettings &s) override;
    void GetAsrSettings(CensorAsrSettings &s) override;
    void GetLexicon(CensorLexicon &l) override { settings_->GetLexicon(l); }
    std::string SourceName() override { return name_; }
    std::filesystem::path PinyinDictDir() override;
    std::unique_ptr<CensorResampler> CreateResampler(uint32_t rate, int layout, size_t channels) override;

    CensorEngine engine{this};

    // Statistics
    std::atomic<float> input_peak{0};     // since last read (reset by reader)
    std::atomic<uint64_t> underruns{0};
    std::atomic<uint64_t> inserted_silence_frames{0};
    std::atomic<uint64_t> dropped_frames{0};
    std::atomic<double> fifo_ms{0};
    std::atomic<double> ratio{1.0};
    DWORD pid() const { return pid_; }

private:
    void OnCapture(const float *interleaved, uint32_t frames, uint64_t qpc, bool discontinuity);
    void Emit(const float *interleaved, uint32_t frames); // nullptr = silence
    void ProcessBlock();
    bool StartCapture(std::string &error);

    Kind kind_;
    std::string name_;
    SharedSettings *settings_;
    std::string exe_;
    DWORD pid_ = 0;
    HANDLE process_ = nullptr;
    uint64_t next_attach_try_ = 0;
    std::unique_ptr<CaptureStream> capture_;
    std::mutex capture_mutex_;
    std::string last_error_;
    FILE *dump_ = nullptr;

    // Capture timeline (capture thread)
    uint64_t last_end_qpc_ = 0; // end time of the audio emitted so far (program captures)
    bool had_idle_ = false;
    uint64_t emitted_ = 0;
    // 10 ms. The recognizer is fed in fixed chunks (feed_chunk_ms), so this block size
    // does not change recognition results.
    static constexpr uint32_t kBlock = 480;
    std::vector<float> block_l_, block_r_;
    uint64_t engine_frames_ = 0;

    // Output FIFO, interleaved stereo
    std::mutex fifo_mutex_;
    std::vector<float> fifo_;
    size_t fifo_read_ = 0, fifo_size_ = 0; // in frames
    size_t FifoCapacity() const { return fifo_.size() / kChannels; }

    // Mixer state (render thread)
    bool primed_ = false;
    double pos_ = 0.0;
    double fill_error_ = 0.0;
};

// Sums all sources into the output stream.
class Mixer {
public:
    void SetSources(std::vector<Source *> sources);
    void Pull(float *out, uint32_t frames);
    std::atomic<float> output_peak{0};

private:
    std::mutex mutex_;
    std::vector<Source *> sources_;
};

// Reads a UTF-8 text file (BOM removed); empty if missing.
std::string ReadTextFile(const std::filesystem::path &path);
// Opts the process out of Windows power throttling (EcoQoS); see app-core.cpp. Call once at start.
bool DisablePowerThrottling();
// 48 kHz stereo 16-bit PCM WAV <-> interleaved float.
bool ReadWav48(const std::filesystem::path &path, std::vector<float> &interleaved);
bool WriteWav48(const std::filesystem::path &path, const std::vector<float> &interleaved);

} // namespace lsf
