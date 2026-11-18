#pragma once

#include "censor-engine.hpp"

namespace lsf {

// Model input resampler with FFmpeg swresample, set up like libobs' audio_resampler
// (default channel layouts, default options) so the 16 kHz model input matches the OBS
// plugin's; the 0.707 * (L + R) downmix is scaled to the plain average as the plugin does.
std::unique_ptr<CensorResampler> CreateSwrResampler(uint32_t rate, size_t channels);

} // namespace lsf
