#include "pipeline.hpp"

namespace lsf {

bool Pipeline::Start(const PipelineConfig &config, SharedSettings *settings, std::string &error) {
    Stop();
    if (config.use_mic) {
        auto src = std::make_unique<Source>(Source::Kind::Device, "麦克风", settings);
        if (!src->OpenDevice(config.mic_id, error)) {
            error = "麦克风“" + config.mic_name + "”无法打开：" + error;
            return false;
        }
        sources_.push_back(std::move(src));
    }
    for (const auto &app : config.apps) {
        auto src = std::make_unique<Source>(Source::Kind::Process, app, settings);
        src->SetProcessName(app);
        src->MaintainProcess();
        sources_.push_back(std::move(src));
    }
    if (sources_.empty()) {
        error = "没有选择要过滤的声音";
        return false;
    }
    std::vector<Source *> raw;
    for (auto &p : sources_) raw.push_back(p.get());
    mixer_.SetSources(raw);
    render_ = std::make_unique<RenderStream>();
    if (!render_->OpenEndpoint(config.out_id, error) ||
        !render_->Start([this](float *o, uint32_t n) { mixer_.Pull(o, n); }, error)) {
        error = "输出设备“" + config.out_name + "”无法打开：" + error;
        render_.reset();
        mixer_.SetSources({});
        sources_.clear();
        return false;
    }
    running_ = true;
    return true;
}

void Pipeline::Stop() {
    if (!running_ && sources_.empty()) return;
    render_.reset(); // stops the stream
    mixer_.SetSources({});
    for (auto &s : sources_) {
        s->Stop();
        s->engine.Stop();
    }
    sources_.clear();
    running_ = false;
}

bool Pipeline::Maintain() {
    if (!running_) return true;
    for (auto &s : sources_) s->MaintainProcess();
    return !render_ || !render_->failed;
}

} // namespace lsf
