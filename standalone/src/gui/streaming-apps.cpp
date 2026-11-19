#include "streaming-apps.hpp"

// The checks themselves are shared with the OBS plugin (upstream src/streaming-apps.cpp).
#include "../../../upstream/obs-profanity-filter/src/streaming-apps.hpp"

std::vector<StreamingCheck> StreamingAppChecks(const QString &cable, const QString &douyin_store) {
    std::vector<StreamingCheck> out;
    for (const StreamingFinding &f : CheckStreamingApps(cable.toStdString(), false, douyin_store.toStdString()).items) {
        const ui::Tone tone = f.level == StreamingFinding::Bad  ? ui::Tone::Bad
                              : f.level == StreamingFinding::Ok ? ui::Tone::Ok
                                                                : ui::Tone::Note;
        QString detail = QString::fromStdString(f.detail + f.basis);
        // Nothing to stream from on this computer: possibly 双机, where this program cannot
        // help (its output stays on this computer); the plugin has a mode for it.
        if (f.kind == StreamingFinding::NoApp)
            detail += QString::fromUtf8("推流软件在另一台电脑（双机推流，经采集卡）时直播消音用不上，"
                                        "请在游戏电脑的 OBS 里用语音消音插件：开播方式选“直播伴侣类推流软件”，推流软件在“另一台电脑”。");
        out.push_back({tone, QString::fromStdString(f.title), detail});
    }
    return out;
}
