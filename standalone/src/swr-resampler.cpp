#include "swr-resampler.hpp"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace lsf {
namespace {

class SwrResampler : public CensorResampler {
public:
    SwrResampler(uint32_t rate, size_t channels) : rate_(rate), channels_(channels) {
        AVChannelLayout in_layout, out_layout;
        av_channel_layout_default(&in_layout, (int)channels);
        av_channel_layout_default(&out_layout, 1);
        if (swr_alloc_set_opts2(&ctx_, &out_layout, AV_SAMPLE_FMT_FLT, 16000, &in_layout, AV_SAMPLE_FMT_FLTP,
                                (int)rate, 0, nullptr) < 0 ||
            swr_init(ctx_) < 0) {
            swr_free(&ctx_);
        }
        av_channel_layout_uninit(&in_layout);
        av_channel_layout_uninit(&out_layout);
    }
    ~SwrResampler() override { swr_free(&ctx_); }
    bool ok() const { return ctx_ != nullptr; }

    bool Resample(const float *const *planes, uint32_t frames, std::vector<float> &out) override {
        // Output size as libobs computes it: pending delay plus this input, rounded up.
        int64_t delay = swr_get_delay(ctx_, rate_);
        int estimated = (int)av_rescale_rnd(delay + (int64_t)frames, 16000, rate_, AV_ROUND_UP);
        buf_.resize((size_t)estimated);
        uint8_t *outp = (uint8_t *)buf_.data();
        int n = swr_convert(ctx_, &outp, estimated, (const uint8_t **)planes, (int)frames);
        if (n < 0) return false;
        const float scale = channels_ == 1 ? 1.0f : 0.70710678f;
        out.reserve(out.size() + (size_t)n);
        for (int i = 0; i < n; i++) out.push_back(buf_[i] * scale);
        return true;
    }

private:
    SwrContext *ctx_ = nullptr;
    uint32_t rate_;
    size_t channels_;
    std::vector<float> buf_;
};

} // namespace

std::unique_ptr<CensorResampler> CreateSwrResampler(uint32_t rate, size_t channels) {
    auto r = std::make_unique<SwrResampler>(rate, channels ? channels : 1);
    if (!r->ok()) return nullptr;
    return r;
}

} // namespace lsf
