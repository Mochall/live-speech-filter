#pragma once

// Checks of the streaming program (推流软件) that takes the filtered voice from CABLE Output:
// 抖音直播伴侣, 直播姬, 虎牙直播 and 斗鱼直播伴侣. The checks are shared with the OBS plugin
// (upstream src/streaming-apps.hpp explains how they work); this turns them into page items.

#include "ui-kit.hpp"

#include <QString>
#include <vector>

struct StreamingCheck {
    ui::Tone tone = ui::Tone::Ok; // Ok, Bad or Note
    QString title;
    QString detail;
};

// cable_recording: the cable's recording endpoint as Windows shows it ("CABLE Output").
// douyin_store: 直播伴侣 audioStore.json (default %APPDATA%/webcast_mate/WBStore/audioStore.json).
std::vector<StreamingCheck> StreamingAppChecks(const QString &cable_recording, const QString &douyin_store);
