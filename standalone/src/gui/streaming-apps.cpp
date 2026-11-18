#include "streaming-apps.hpp"

// The checks themselves are shared with the OBS plugin (upstream src/streaming-apps.cpp).
#include "../../../upstream/obs-profanity-filter/src/streaming-apps.hpp"

std::vector<StreamingCheck> StreamingAppChecks(const QString &cable, const QString &douyin_store) {
    std::vector<StreamingCheck> out;
    for (const StreamingFinding &f : CheckStreamingApps(cable.toStdString(), false, douyin_store.toStdString()).items) {
        const ui::Tone tone = f.level == StreamingFinding::Bad  ? ui::Tone::Bad
                              : f.level == StreamingFinding::Ok ? ui::Tone::Ok
                                                                : ui::Tone::Note;
        out.push_back({tone, QString::fromStdString(f.title), QString::fromStdString(f.detail + f.basis)});
    }
    return out;
}
