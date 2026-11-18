#pragma once

// Running filter for the window program: the selected microphone and programs as Sources,
// the Mixer and the output stream to VB-CABLE Input. Start/Stop from the UI thread.

#include "app-core.hpp"

#include <memory>
#include <string>
#include <vector>

namespace lsf {

struct PipelineConfig {
    bool use_mic = true;
    std::wstring mic_id;              // capture endpoint id
    std::string mic_name;
    std::vector<std::string> apps;    // executable names, e.g. "KOOK.exe"
    std::wstring out_id;              // render endpoint id (CABLE Input)
    std::string out_name;
};

class Pipeline {
public:
    ~Pipeline() { Stop(); }
    bool Start(const PipelineConfig &config, SharedSettings *settings, std::string &error);
    void Stop();
    bool running() const { return running_; }
    // Re-attaches programs that (re)started; call a few times per second while running.
    // Returns false when the output stream failed (reason in failure()).
    bool Maintain();
    std::string failure() const { return render_ ? render_->failure : std::string(); }
    const std::vector<std::unique_ptr<Source>> &sources() const { return sources_; }
    Mixer &mixer() { return mixer_; }

private:
    std::vector<std::unique_ptr<Source>> sources_;
    Mixer mixer_;
    std::unique_ptr<RenderStream> render_; // new one per start (a stream opens its device once)
    bool running_ = false;
};

} // namespace lsf
