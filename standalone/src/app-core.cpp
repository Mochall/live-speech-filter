#include "app-core.hpp"
#include "swr-resampler.hpp"
#include "censor-log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace lsf {

bool DisablePowerThrottling() {
    // Windows 11 runs a process with no visible window as "background" (EcoQoS): its threads go to
    // the efficiency cores at low clock. Lite sits in the tray while a game is in front, and 4
    // recognizer streams then fall behind real time (file test 2026-11-14, i7-14700HX: every word
    // 3-8 s late; with this call 0 late). Priority is unchanged, so the game still comes first.
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0; // never throttle
    return SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state)) != 0;
}

std::string ReadTextFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s.erase(0, 3);
    return s;
}

bool ReadWav48(const std::filesystem::path &path, std::vector<float> &out) {
    std::ifstream in(path, std::ios::binary);
    char riff[12];
    if (!in.read(riff, 12) || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) return false;
    bool fmt_ok = false;
    for (;;) {
        char id[4];
        uint32_t size;
        if (!in.read(id, 4) || !in.read((char *)&size, 4)) return false;
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

bool WriteWav48(const std::filesystem::path &path, const std::vector<float> &x) {
    std::ofstream o(path, std::ios::binary);
    if (!o) return false;
    uint32_t data = (uint32_t)(x.size() * 2), riff = 36 + data, fmt_size = 16, rate = 48000, bps = 48000 * 4;
    uint16_t tag = 1, ch = 2, align = 4, bits = 16;
    o.write("RIFF", 4); o.write((char *)&riff, 4); o.write("WAVE", 4);
    o.write("fmt ", 4); o.write((char *)&fmt_size, 4); o.write((char *)&tag, 2); o.write((char *)&ch, 2);
    o.write((char *)&rate, 4); o.write((char *)&bps, 4); o.write((char *)&align, 2); o.write((char *)&bits, 2);
    o.write("data", 4); o.write((char *)&data, 4);
    for (float v : x) {
        int16_t s = (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
        o.write((char *)&s, 2);
    }
    return (bool)o;
}

// ---------------------------------------------------------------------------------------

void SharedSettings::Set(const AppSettings &s) {
    std::string words = CensorBuildWordList(s.builtin_words, s.custom_words);
    std::vector<std::regex> patterns = CensorParsePatterns(words);
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = s;
    word_list_ = std::move(words);
    patterns_ = std::move(patterns);
}

AppSettings SharedSettings::Get() {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

void SharedSettings::GetLexicon(CensorLexicon &l) {
    std::lock_guard<std::mutex> lock(mutex_);
    l.patterns = patterns_;
    l.use_pinyin = settings_.use_pinyin;
    l.dirty_words = word_list_;
    l.pinyin_words = settings_.pinyin_words;
}

// ---------------------------------------------------------------------------------------

Source::Source(Kind kind, std::string name, SharedSettings *settings)
    : kind_(kind), name_(std::move(name)), settings_(settings) {
    fifo_.assign((size_t)kRate * kChannels, 0.0f); // 1 s
    block_l_.reserve(kBlock);
    block_r_.reserve(kBlock);
}

Source::~Source() {
    Stop();
    engine.Stop(); // the recognition thread calls back into this object
    if (dump_) fclose(dump_);
}

void Source::SetDumpFile(const std::filesystem::path &path) {
    if (dump_) fclose(dump_);
    dump_ = _wfopen(path.c_str(), L"wb");
}

void Source::GetAudioSettings(CensorAudioSettings &s) {
    AppSettings a = settings_->Get();
    s.enable = a.enable;
    s.model_path = a.model_path;
    s.delay_seconds = a.delay_seconds;
    s.effect = a.effect;
    s.clip = a.clip;
    s.clip_rate = 48000;
    s.clip_period = a.clip_period;
    s.beep_frequency = 1000;
    s.beep_mix_percent = 100;
}

void Source::GetAsrSettings(CensorAsrSettings &s) {
    AppSettings a = settings_->Get();
    s.enable = a.enable;
    s.model_path = a.model_path;
    s.provider = "cpu";
    s.censor_padding_ms = a.censor_padding_ms;
    s.model_offset_ms = a.model_offset_ms;
    s.debug_logging = a.debug_logging;
    s.fixed_chunk_16k = a.feed_chunk_ms * 16; // fixed size: results independent of capture/thread timing
    s.recognizer_lanes = a.recognizer_lanes;
    s.lane_step_16k = a.lane_step_ms * 16;
}

std::filesystem::path Source::PinyinDictDir() {
    std::filesystem::path p = settings_->Get().dict_dir;
    std::error_code ec;
    return std::filesystem::exists(p, ec) ? p : std::filesystem::path();
}

std::unique_ptr<CensorResampler> Source::CreateResampler(uint32_t rate, int, size_t channels) {
    return CreateSwrResampler(rate, channels);
}

std::string Source::capture_error() {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    if (capture_ && capture_->failed) return capture_->failure;
    return last_error_;
}

bool Source::StartCapture(std::string &error) {
    last_end_qpc_ = 0;
    had_idle_ = false;
    return capture_->Start([this](const float *d, uint32_t f, uint64_t q, bool disc) { OnCapture(d, f, q, disc); }, error);
}

bool Source::OpenDevice(const std::wstring &endpoint_id, std::string &error) {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    capture_ = std::make_unique<CaptureStream>();
    if (!capture_->OpenEndpoint(endpoint_id, error) || !StartCapture(error)) {
        last_error_ = error;
        capture_.reset();
        return false;
    }
    engine.Start();
    return true;
}

void Source::MaintainProcess() {
    if (kind_ != Kind::Process) return;
    engine.Start();
    std::lock_guard<std::mutex> lock(capture_mutex_);
    bool exited = process_ && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0;
    bool failed = capture_ && capture_->failed;
    if (exited || failed) {
        if (capture_) capture_->Stop();
        capture_.reset();
        if (process_) CloseHandle(process_);
        process_ = nullptr;
        if (exited) CLOG(censor::kLogInfo, "[Lite] %s: %s exited", name_.c_str(), exe_.c_str());
        else CLOG(censor::kLogWarning, "[Lite] %s: capture failed, re-attaching", name_.c_str());
        pid_ = 0;
    }
    if (pid_) return;
    uint64_t now = QpcNow100ns();
    if (now < next_attach_try_) return;
    next_attach_try_ = now + 20000000ULL; // 2 s
    DWORD pid = FindProcessRoot(exe_);
    if (!pid) {
        last_error_ = exe_ + " is not running";
        return;
    }
    auto cap = std::make_unique<CaptureStream>();
    std::string error;
    capture_ = std::move(cap);
    if (!capture_->OpenProcess(pid, error) || !StartCapture(error)) {
        last_error_ = error;
        capture_.reset();
        CLOG(censor::kLogWarning, "[Lite] %s: attach to %s (pid %lu) failed: %s", name_.c_str(), exe_.c_str(),
             (unsigned long)pid, error.c_str());
        return;
    }
    process_ = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
    pid_ = pid;
    last_error_.clear();
    CLOG(censor::kLogInfo, "[Lite] %s: capturing %s (pid %lu and child processes)", name_.c_str(), exe_.c_str(),
         (unsigned long)pid);
}

void Source::Stop() {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    if (capture_) capture_->Stop();
    capture_.reset();
    if (process_) CloseHandle(process_);
    process_ = nullptr;
    pid_ = 0;
}

// Capture thread. Program captures deliver no packets while the program is silent; those
// pauses are filled with silence at real time so the delay line keeps moving (the delayed
// end of an utterance still plays) and the recognizer timeline stays continuous. Packets
// are never dropped; a packet that overlaps the filled silence just shifts the timeline.
void Source::OnCapture(const float *interleaved, uint32_t frames, uint64_t qpc, bool) {
    const uint64_t frame_100ns = 10000000ULL / kRate; // not exact (208.33), only for thresholds
    if (kind_ == Kind::Process) {
        if (!frames) {
            if (!last_end_qpc_) last_end_qpc_ = qpc; // nothing played since attaching
            // Fill up to 10 ms before now once more than 30 ms passed without packets.
            if (qpc > last_end_qpc_ + 300000ULL) {
                uint64_t fill_to = qpc - 100000ULL;
                uint32_t n = (uint32_t)((fill_to - last_end_qpc_) * kRate / 10000000ULL);
                if (n) {
                    Emit(nullptr, n);
                    inserted_silence_frames += n;
                    last_end_qpc_ += (uint64_t)n * 10000000ULL / kRate;
                }
                had_idle_ = true;
            }
            return;
        }
        if (last_end_qpc_ && had_idle_ && qpc > last_end_qpc_ + 200000ULL) {
            uint32_t n = (uint32_t)((qpc - last_end_qpc_) * kRate / 10000000ULL);
            Emit(nullptr, n);
            inserted_silence_frames += n;
        }
        had_idle_ = false;
        last_end_qpc_ = qpc + (uint64_t)frames * 10000000ULL / kRate;
        (void)frame_100ns;
    } else if (!frames) {
        return;
    }
    float peak = 0;
    for (size_t i = 0; i < (size_t)frames * kChannels; i++) peak = std::max(peak, std::fabs(interleaved[i]));
    if (peak > input_peak.load()) input_peak = peak;
    if (dump_) fwrite(interleaved, sizeof(float) * kChannels, frames, dump_);
    Emit(interleaved, frames);
}

void Source::Emit(const float *interleaved, uint32_t frames) {
    for (uint32_t i = 0; i < frames; i++) {
        block_l_.push_back(interleaved ? interleaved[i * 2] : 0.0f);
        block_r_.push_back(interleaved ? interleaved[i * 2 + 1] : 0.0f);
        if (block_l_.size() == kBlock) ProcessBlock();
    }
    emitted_ += frames;
}

void Source::ProcessBlock() {
    float *planes[CensorEngine::kMaxPlanes] = {block_l_.data(), block_r_.data()};
    // Continuous timeline: the engine never sees a gap.
    uint64_t ts = 1000000ULL + engine_frames_ * 1000000000ULL / kRate;
    engine.Process(planes, kBlock, ts, kRate, 2);
    engine_frames_ += kBlock;
    {
        std::lock_guard<std::mutex> lock(fifo_mutex_);
        const size_t cap = FifoCapacity();
        if (fifo_size_ + kBlock > cap) {
            size_t drop = fifo_size_ + kBlock - cap;
            fifo_read_ = (fifo_read_ + drop) % cap;
            fifo_size_ -= drop;
            dropped_frames += drop;
        }
        size_t w = (fifo_read_ + fifo_size_) % cap;
        for (uint32_t i = 0; i < kBlock; i++) {
            fifo_[w * 2] = block_l_[i];
            fifo_[w * 2 + 1] = block_r_[i];
            w = (w + 1) % cap;
        }
        fifo_size_ += kBlock;
    }
    block_l_.clear();
    block_r_.clear();
}

// Render thread.
void Source::MixInto(float *out, uint32_t frames) {
    const double target = kRate * 0.040; // 40 ms
    std::lock_guard<std::mutex> lock(fifo_mutex_);
    const size_t cap = FifoCapacity();
    if (fifo_size_ > target + kRate * 0.2) {
        // Far too much buffered (output started late or stalled): skip back to the target.
        size_t drop = fifo_size_ - (size_t)target;
        fifo_read_ = (fifo_read_ + drop) % cap;
        fifo_size_ -= drop;
        dropped_frames += drop;
        fill_error_ = 0;
    }
    if (!primed_) {
        if (fifo_size_ < target) {
            fifo_ms = fifo_size_ * 1000.0 / kRate;
            return;
        }
        primed_ = true;
        pos_ = 0;
        fill_error_ = 0;
    }
    fill_error_ += 0.02 * ((double)fifo_size_ - target - fill_error_);
    // 10 ms of fill error -> 0.1 % faster/slower, at most 0.3 %.
    double r = 1.0 + std::clamp(fill_error_ / 480.0 * 0.001, -0.003, 0.003);
    size_t need = (size_t)(pos_ + (frames - 1) * r) + 2;
    if (fifo_size_ < need) {
        underruns++;
        primed_ = false;
        fifo_ms = fifo_size_ * 1000.0 / kRate;
        return;
    }
    for (uint32_t i = 0; i < frames; i++) {
        double p = pos_ + i * r;
        size_t idx = (size_t)p;
        float t = (float)(p - idx);
        size_t a = (fifo_read_ + idx) % cap, b = (a + 1) % cap;
        out[i * 2] += fifo_[a * 2] * (1.0f - t) + fifo_[b * 2] * t;
        out[i * 2 + 1] += fifo_[a * 2 + 1] * (1.0f - t) + fifo_[b * 2 + 1] * t;
    }
    double end = pos_ + frames * r;
    size_t consumed = (size_t)end;
    pos_ = end - consumed;
    fifo_read_ = (fifo_read_ + consumed) % cap;
    fifo_size_ -= consumed;
    fifo_ms = fifo_size_ * 1000.0 / kRate;
    ratio = r;
}

// ---------------------------------------------------------------------------------------

void Mixer::SetSources(std::vector<Source *> sources) {
    std::lock_guard<std::mutex> lock(mutex_);
    sources_ = std::move(sources);
}

void Mixer::Pull(float *out, uint32_t frames) {
    std::fill(out, out + (size_t)frames * kChannels, 0.0f);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Source *s : sources_) s->MixInto(out, frames);
    }
    float peak = 0;
    for (size_t i = 0; i < (size_t)frames * kChannels; i++) {
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
        peak = std::max(peak, std::fabs(out[i]));
    }
    if (peak > output_peak.load()) output_peak = peak;
}

} // namespace lsf
