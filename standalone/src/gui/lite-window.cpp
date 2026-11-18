#include "lite-window.hpp"

#include "audio-io.hpp"
#include "cable-setup.hpp"
#include "censor-engine.hpp"
#include "censor-log.hpp"
#include "companion-audio.hpp"
#include "lite-checks.hpp"
#include "streaming-apps.hpp"
#include "tray-popup.hpp"
#include "utils.hpp"

#include <windows.h>
#include <dwmapi.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <windowsx.h>

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

using namespace lsf;
using ui::Tone;

namespace {

QString Q(const std::string &s) { return QString::fromStdString(s); }

QString ReadText(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QString t = QString::fromUtf8(f.readAll());
    if (t.startsWith(QChar(0xfeff))) t.remove(0, 1);
    return t;
}

bool WriteText(const QString &path, const QString &text, QString &error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    QByteArray bytes = text.toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        error = f.errorString();
        return false;
    }
    return true;
}

QStringList WordLines(const QString &text) {
    QStringList out;
    for (const auto &w : SplitWordList(text.toStdString())) out << Q(w);
    return out;
}

const char *kVbCableUrl = "https://vb-audio.com/Cable/";
const char *kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char *kRunName = "LiveSpeechFilter";
constexpr double kSilentDb = -45.0;  // microphone level counted as sound
constexpr double kSilentSeconds = 30.0;

double Db(float peak) { return peak > 1e-6f ? 20.0 * std::log10(peak) : -120.0; }
float Level01(double db) { return (float)std::clamp((db + 60.0) / 60.0, 0.0, 1.0); }

// Sets a style-sheet property and re-applies the style when it changed.
void SetProp(QWidget *w, const char *name, const QVariant &value) {
    if (w->property(name) == value) return;
    w->setProperty(name, value);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

void ClearLayout(QLayout *layout) {
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        } else if (QLayout *l = item->layout()) {
            ClearLayout(l);
        }
        delete item;
    }
}

struct PageParts {
    QWidget *page;
    QVBoxLayout *body;
    QHBoxLayout *header;
    QLabel *meta;
    QLabel *title;
};

PageParts MakePage(const QString &title, const QString &meta) {
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(28, 22, 28, 28);
    v->setSpacing(14);
    auto *hd = new QHBoxLayout;
    hd->setSpacing(10);
    auto *tv = new QVBoxLayout;
    tv->setSpacing(2);
    auto *h1 = new QLabel(title);
    h1->setProperty("role", "h1");
    auto *m = new QLabel(meta);
    m->setProperty("role", "meta");
    m->setWordWrap(true);
    tv->addWidget(h1);
    tv->addWidget(m);
    hd->addLayout(tv, 1);
    v->addLayout(hd);
    auto *scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return {scroll, v, hd, m, h1};
}

QVBoxLayout *CardBody(QWidget *card, int top = 8) {
    auto *b = new QVBoxLayout;
    b->setContentsMargins(16, top, 16, 14);
    b->setSpacing(10);
    qobject_cast<QVBoxLayout *>(card->layout())->addLayout(b);
    return b;
}

// "label | content" line of a settings card; returns the content column.
QVBoxLayout *FormRow(QVBoxLayout *body, const QString &label) {
    auto *h = new QHBoxLayout;
    h->setSpacing(12);
    auto *l = new QLabel(label);
    l->setProperty("role", "label");
    l->setFixedWidth(104);
    l->setContentsMargins(0, 5, 0, 0);
    h->addWidget(l, 0, Qt::AlignTop);
    auto *v = new QVBoxLayout;
    v->setSpacing(6);
    h->addLayout(v, 1);
    body->addLayout(h);
    return v;
}

// Same models and download addresses as the plugin's data/models.json (ModelScope, zip files).
struct CatalogModel {
    const char *id, *name, *size, *note, *url;
};
const CatalogModel kCatalog[] = {
    {"sherpa-onnx-x-asr-160ms-streaming-zipformer-transducer-zh-en-int8-2026-06-05", "推荐", "130 MB",
     "中英双语，160 毫秒流式识别。延迟、漏词和误消音之间最平衡，本程序的测试都用它。", ""},
    {"sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23", "轻量", "54 MB", "只认中文，占用最少，漏词比推荐模型多。",
     "https://modelscope.cn/models/cloud370/obs-profanity-filter/resolve/master/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23.zip"},
    {"sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20", "标准", "357 MB", "中英双语。",
     "https://modelscope.cn/models/cloud370/obs-profanity-filter/resolve/master/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20.zip"},
    {"sherpa-onnx-streaming-zipformer-zh-2025-06-30", "最强", "597 MB", "只认中文，最准，最占 CPU。",
     "https://modelscope.cn/models/cloud370/obs-profanity-filter/resolve/master/sherpa-onnx-streaming-zipformer-zh-2025-06-30.zip"},
};

QString FolderSize(const QString &dir) {
    qint64 bytes = 0;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) bytes += it.nextFileInfo().size();
    return QString("%1 MB").arg((bytes + 512 * 1024) / (1024 * 1024));
}

// 48 kHz mono 16-bit WAV of the replacement sound the audience hears, as the engine makes it.
QByteArray EffectWav(int effect) {
    const int rate = 48000, n = rate * 9 / 10;
    QByteArray pcm(n * 2, 0);
    auto *out = reinterpret_cast<int16_t *>(pcm.data());
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < n; i++) {
        const double t = (double)i / rate;
        double v = 0;
        if (effect == 3) v = (std::sin(2 * pi * 8 * t) + std::sin(2 * pi * 3 * t) > 0) ? 0.15 * std::sin(2 * pi * 750 * t) : 0;
        else if (effect == 0) v = 0.1 * std::sin(2 * pi * 1000 * t);
        const double fade = std::min({1.0, i / 240.0, (n - i) / 240.0}); // no clicks at the ends
        out[i] = (int16_t)std::lround(v * fade * 32767);
    }
    QByteArray wav;
    auto u32 = [&](uint32_t x) { wav.append(reinterpret_cast<const char *>(&x), 4); };
    auto u16 = [&](uint16_t x) { wav.append(reinterpret_cast<const char *>(&x), 2); };
    wav.append("RIFF");
    u32(36 + (uint32_t)pcm.size());
    wav.append("WAVEfmt ");
    u32(16), u16(1), u16(1), u32(rate), u32(rate * 2), u16(2), u16(16);
    wav.append("data");
    u32((uint32_t)pcm.size());
    wav.append(pcm);
    return wav;
}

const char *kEffectHelp[] = {"观众在屏蔽词处听到“哔”。", "观众在屏蔽词处听到一段静音。", "", "屏蔽词换成一串电报“嘀嗒”声。"};

// Animal sounds (CC0, data/sounds/README.md). Saved as effect 10 + index; the engine gets effect 4
// with the clip, repeated every clip length + gap while the mute lasts.
struct SoundEffect {
    const char *name, *file, *help;
    int gap_ms;
};
const SoundEffect kSounds[] = {
    {"鸟语", "bird.wav", "屏蔽词换成一段鸟叫。", 0},
    {"猫叫", "cat.wav", "屏蔽词换成“喵”。", 250},
    {"狗叫", "dog.wav", "屏蔽词换成“汪汪”。", 180},
    {"鸭子叫", "duck.wav", "屏蔽词换成“嘎嘎”。", 200},
    {"羊叫", "sheep.wav", "屏蔽词换成“咩”。", 250},
};
constexpr int kSoundBase = 10;

// 48 kHz mono 16-bit PCM WAV as shipped in data/sounds; anything else is refused.
std::shared_ptr<const std::vector<float>> LoadClip(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    const QByteArray b = f.readAll();
    if (b.size() < 44 || !b.startsWith("RIFF") || b.mid(8, 4) != "WAVE") return nullptr;
    qsizetype pos = 12;
    int rate = 0, channels = 0, bits = 0;
    while (pos + 8 <= b.size()) {
        const QByteArray id = b.mid(pos, 4);
        uint32_t len;
        std::memcpy(&len, b.constData() + pos + 4, 4);
        pos += 8;
        if (id == "fmt " && len >= 16 && pos + 16 <= b.size()) {
            uint16_t fmt, ch, bps;
            uint32_t sr;
            std::memcpy(&fmt, b.constData() + pos, 2);
            std::memcpy(&ch, b.constData() + pos + 2, 2);
            std::memcpy(&sr, b.constData() + pos + 4, 4);
            std::memcpy(&bps, b.constData() + pos + 14, 2);
            if (fmt != 1) return nullptr;
            rate = (int)sr, channels = ch, bits = bps;
        } else if (id == "data") {
            if (rate != 48000 || channels != 1 || bits != 16) return nullptr;
            const qsizetype n = std::min<qsizetype>(len, b.size() - pos) / 2;
            auto out = std::make_shared<std::vector<float>>(n);
            const auto *in = reinterpret_cast<const int16_t *>(b.constData() + pos);
            for (qsizetype i = 0; i < n; i++) (*out)[i] = in[i] / 32768.0f;
            return out;
        }
        pos += len + (len & 1);
    }
    return nullptr;
}

QByteArray PcmWav(const std::vector<int16_t> &pcm16) {
    QByteArray pcm(reinterpret_cast<const char *>(pcm16.data()), (qsizetype)pcm16.size() * 2);
    QByteArray wav;
    auto u32 = [&](uint32_t x) { wav.append(reinterpret_cast<const char *>(&x), 4); };
    auto u16 = [&](uint16_t x) { wav.append(reinterpret_cast<const char *>(&x), 2); };
    wav.append("RIFF");
    u32(36 + (uint32_t)pcm.size());
    wav.append("WAVEfmt ");
    u32(16), u16(1), u16(1), u32(48000), u32(96000), u16(2), u16(16);
    wav.append("data");
    u32((uint32_t)pcm.size());
    wav.append(pcm);
    return wav;
}

// What the audience hears for a 1.5 s mute with a clip, as the engine plays it.
QByteArray ClipWav(const std::vector<float> &clip, size_t period) {
    const int n = 48000 * 3 / 2;
    std::vector<int16_t> out(n);
    period = std::max(period, clip.size());
    for (int i = 0; i < n; i++) {
        const size_t k = i % period;
        const double fade = std::min({1.0, i / 240.0, (n - i) / 240.0});
        out[i] = (int16_t)std::lround((k < clip.size() ? clip[k] : 0.0f) * fade * 32767);
    }
    return PcmWav(out);
}

} // namespace

// ============================================================================= construction

static QString CableRecordingName();

LiteWindow::LiteWindow(const LitePaths &paths, QWidget *parent) : QMainWindow(parent), paths_(paths) {
    setWindowTitle("直播消音");
    setWindowIcon(ui::TrayIcon(Tone::Off));

    setWindowFlag(Qt::FramelessWindowHint);
    net_ = new QNetworkAccessManager(this);
    cable_setup_ = new CableSetup(net_, paths_.config_dir + "/vbcable",
                                  QCoreApplication::applicationDirPath() + "/vbcable/VBCABLE_Driver_Pack46.zip", this);
    connect(cable_setup_, &CableSetup::Changed, this, [this] { UpdateChecks(); });
    connect(cable_setup_, &CableSetup::Finished, this, &LiteWindow::CableSetupFinished);
    auto *root = new QWidget;
    root->setObjectName("root");
    root->setAttribute(Qt::WA_StyledBackground);
    auto *rv = new QVBoxLayout(root);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);
    rv->addWidget(BuildTitleBar());
    auto *h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    rv->addLayout(h, 1);
    h->addWidget(BuildSidebar());
    auto *main = new QWidget;
    main->setObjectName("main");
    main->setAttribute(Qt::WA_StyledBackground);
    auto *mv = new QVBoxLayout(main);
    mv->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget;
    pages_->addWidget(BuildFilterPage());
    pages_->addWidget(BuildWordsPage());
    pages_->addWidget(BuildSettingsPage());
    pages_->addWidget(BuildModelPage());
    pages_->addWidget(BuildCheckPage());
    pages_->addWidget(BuildSupportPage());
    mv->addWidget(pages_);
    h->addWidget(main, 1);
    setCentralWidget(root);
    resize(960, 680);

    // Give the frameless window back the native styles that bring the resize border, shadow,
    // snapping and minimize/maximize animations; WM_NCCALCSIZE removes the native title bar again.
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    SetWindowLongPtrW(hwnd, GWL_STYLE,
                      GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    const MARGINS shadow{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &shadow);
    const DWORD round = 2; // DWMWCP_ROUND (Windows 11; ignored before)
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    for (const auto &sound : kSounds) {
        clips_.push_back(LoadClip(paths_.exe_dir + "/data/sounds/" + sound.file));
        if (!clips_.back()) CLOG(censor::kLogWarning, "[Lite] sound missing or not 48 kHz mono 16-bit: data/sounds/%s", sound.file);
    }
    FillDevices();
    LoadConfig();
    ScanModels();
    RebuildWordList();
    loading_ = false;
    SetSourcesEditable(true);
    ApplySettings();
    UpdateDelayUi();
    BuildTray();

    tick_timer_ = new QTimer(this);
    connect(tick_timer_, &QTimer::timeout, this, &LiteWindow::Tick);
    tick_timer_->start(100);
    check_timer_ = new QTimer(this);
    connect(check_timer_, &QTimer::timeout, this, &LiteWindow::UpdateChecks);
    check_timer_->start(2000);
    UpdateChecks();
    Tick();
}

LiteWindow::~LiteWindow() {
    PlaySoundW(nullptr, nullptr, 0);
    if (download_) download_->abort();
    pipeline_.Stop();
    delete popup_;
}

QWidget *LiteWindow::BuildSidebar() {
    auto *side = new QWidget;
    side->setObjectName("side");
    side->setAttribute(Qt::WA_StyledBackground);
    side->setFixedWidth(196);
    auto *v = new QVBoxLayout(side);
    v->setContentsMargins(12, 14, 12, 12);
    v->setSpacing(2);
    auto *name = new QLabel("直播消音");
    name->setObjectName("appname");
    v->addWidget(name);

    nav_ = new QButtonGroup(this);
    nav_->setExclusive(true);
    auto add = [&](const QString &text, ui::Glyph glyph, Page page) {
        auto *b = new QPushButton(text);
        b->setProperty("nav", "true");
        b->setCheckable(true);
        b->setIcon(ui::NavIcon(glyph));
        b->setIconSize(QSize(18, 18));
        b->setCursor(Qt::PointingHandCursor);
        nav_->addButton(b, page);
        v->addWidget(b);
        auto *l = new QHBoxLayout(b);
        l->setContentsMargins(0, 0, 8, 0);
        l->addStretch();
        return l;
    };
    add("过滤", ui::Glyph::Filter, kFilter);
    words_count_ = new QLabel;
    words_count_->setProperty("role", "navcount");
    add("屏蔽词", ui::Glyph::Words, kWords)->addWidget(words_count_);
    add("消音设置", ui::Glyph::Settings, kSettings);
    add("识别模型", ui::Glyph::Model, kModel);
    check_badge_ = new QLabel;
    check_badge_->setProperty("role", "badge");
    check_badge_->hide();
    add("开播前检查", ui::Glyph::Check, kCheck)->addWidget(check_badge_);
    v->addStretch();
    auto *foot = new QLabel("关掉窗口仍在过滤，程序留在托盘里");
    foot->setProperty("role", "meta");
    foot->setWordWrap(true);
    foot->setContentsMargins(8, 0, 8, 6);
    v->addWidget(foot);
    add("支持项目", ui::Glyph::Heart, kSupport);
    nav_->button(kFilter)->setChecked(true);
    connect(nav_, &QButtonGroup::idClicked, this, [this](int id) { pages_->setCurrentIndex(id); });
    return side;
}

QWidget *LiteWindow::BuildFilterPage() {
    PageParts p = MakePage("未在过滤", "");
    title_ = p.title;
    subtitle_ = p.meta;
    chip_ = ui::MakeChip();
    p.header->addWidget(chip_, 0, Qt::AlignVCenter);
    go_ = ui::Pill("开始过滤", "acc");
    connect(go_, &QPushButton::clicked, this, &LiteWindow::ToggleFilter);
    p.header->addWidget(go_, 0, Qt::AlignVCenter);

    banner_ = new QLabel;
    banner_->setProperty("role", "banner");
    banner_->setWordWrap(true);
    banner_->setTextFormat(Qt::RichText);
    banner_->hide();
    connect(banner_, &QLabel::linkActivated, this, [this](const QString &link) { ShowPage((Page)link.toInt()); });
    p.body->addWidget(banner_);

    // Sources
    QWidget *card = ui::Card("要过滤的声音", &lock_hint_);
    QVBoxLayout *body = CardBody(card, 4);
    body->setSpacing(0);
    auto *ruler = new QHBoxLayout;
    ruler->setContentsMargins(0, 0, 0, 2);
    const char *marks[] = {"30 秒前", "20", "10", "现在"};
    for (int i = 0; i < 4; i++) {
        auto *l = new QLabel(marks[i]);
        l->setProperty("role", "meta");
        ruler->addWidget(l);
        if (i < 3) ruler->addStretch();
    }
    body->addLayout(ruler);
    source_rows_ = new QVBoxLayout;
    source_rows_->setSpacing(0);
    body->addLayout(source_rows_);

    auto mic = std::make_unique<Row>();
    mic->is_mic = true;
    Row *m = mic.get();
    rows_.push_back(std::move(mic));
    m->frame = new QFrame;
    m->frame->setProperty("row", "true");
    auto *mv = new QVBoxLayout(m->frame);
    mv->setContentsMargins(0, 10, 0, 10);
    mv->setSpacing(6);
    auto *mh = new QHBoxLayout;
    m->check = new QCheckBox("麦克风");
    m->device = new QComboBox;
    m->device->setMinimumWidth(260);
    m->state = new QLabel;
    m->state->setProperty("role", "state");
    mh->addWidget(m->check);
    mh->addWidget(m->device);
    mh->addStretch();
    mh->addWidget(m->state);
    mv->addLayout(mh);
    m->track = new ui::TrackView;
    m->track->setFixedHeight(52);
    mv->addWidget(m->track);
    source_rows_->addWidget(m->frame);
    connect(m->check, &QCheckBox::toggled, this, [this] { SaveConfig(); UpdateChecks(); });
    connect(m->device, &QComboBox::currentIndexChanged, this, [this] { SaveConfig(); UpdateChecks(); });

    auto *addRow = new QHBoxLayout;
    addRow->setContentsMargins(0, 10, 0, 6);
    add_program_ = ui::Link("＋ 添加要过滤的程序");
    connect(add_program_, &QPushButton::clicked, this, &LiteWindow::ShowAddProgramsDialog);
    addRow->addWidget(add_program_);
    addRow->addSpacing(12);
    auto *scope = new QLabel("游戏、音乐等其他声音不经过这里，在推流软件里单独采集");
    scope->setProperty("role", "meta");
    addRow->addWidget(scope);
    addRow->addStretch();
    body->addLayout(addRow);
    // Lite delays only the voice it filters (~1.4 s), never the picture: a face camera or a game
    // whose own voice chat is filtered would be out of sync. The OBS plugin delays the picture too.
    // "推流软件" = any streaming program; 直播伴侣 is named only where its own settings are read.
    auto *plugin = new QLabel("需要音画同步的场景（露脸、过滤游戏或其他程序里的语音）请用 OBS 插件：本程序只延迟声音，不延迟画面");
    plugin->setProperty("role", "meta");
    plugin->setWordWrap(true);
    body->addWidget(plugin);
    body->addSpacing(8);

    auto *outFrame = new QFrame;
    outFrame->setProperty("row", "true");
    auto *ov = new QVBoxLayout(outFrame);
    ov->setContentsMargins(0, 10, 0, 0);
    ov->setSpacing(4);
    auto *oh = new QHBoxLayout;
    auto *ol = new QLabel("消音后的声音送到");
    ol->setProperty("role", "label");
    oh->addWidget(ol);
    output_ = new QComboBox;
    output_->setMinimumWidth(300);
    oh->addWidget(output_);
    oh->addStretch();
    ov->addLayout(oh);
    ov->addWidget(ui::Help("推流软件的麦克风只选“" + CableRecordingName() + "”，并关闭系统声音采集。"));
    body->addWidget(outFrame);
    connect(output_, &QComboBox::currentIndexChanged, this, [this] { SaveConfig(); UpdateChecks(); });
    p.body->addWidget(card);

    // Check summary
    QLabel *note = nullptr;
    QHBoxLayout *titleRow = nullptr;
    QWidget *checks = ui::Card("开播前检查", &note, &titleRow);
    note->setText("每 2 秒刷新");
    auto *all = ui::Link("看全部");
    all->setStyleSheet("font-size:12px");
    connect(all, &QPushButton::clicked, this, [this] { ShowPage(kCheck); });
    titleRow->addWidget(all);
    check_summary_ = new QVBoxLayout;
    check_summary_->setContentsMargins(0, 4, 0, 4);
    check_summary_->setSpacing(0);
    qobject_cast<QVBoxLayout *>(checks->layout())->addLayout(check_summary_);
    p.body->addWidget(checks);
    p.body->addStretch();
    return p.page;
}

QWidget *LiteWindow::BuildWordsPage() {
    PageParts p = MakePage("屏蔽词", "内置词库是常见脏话，随程序更新，不提供查看。其他要屏蔽的词（如带货违禁词）在这里自己添加，添加后立即生效。");
    QWidget *card = ui::Card();
    QVBoxLayout *body = CardBody(card, 14);
    auto *add = new QHBoxLayout;
    add->setSpacing(10);
    word_edit_ = new QLineEdit;
    word_edit_->setPlaceholderText("输入一个词，写普通文字即可，例如某个外号");
    add->addWidget(word_edit_, 1);
    word_kind_ = new ui::Segmented;
    word_kind_->AddOption("屏蔽", false);
    word_kind_->AddOption("例外，不屏蔽", true);
    word_kind_->SetCurrent(0);
    add->addWidget(word_kind_);
    auto *addButton = ui::Pill("添加", "acc");
    add->addWidget(addButton);
    body->addLayout(add);
    connect(addButton, &QPushButton::clicked, this, &LiteWindow::AddWord);
    connect(word_edit_, &QLineEdit::returnPressed, this, &LiteWindow::AddWord);
    auto *listHead = new QHBoxLayout;
    auto *listTitle = new QLabel("我加的词");
    listTitle->setProperty("role", "bold");
    listHead->addWidget(listTitle);
    listHead->addStretch();
    words_edit_ = ui::Link("编辑");
    connect(words_edit_, &QPushButton::clicked, this, [this] {
        editing_words_ = !editing_words_;
        RebuildWordList();
    });
    listHead->addWidget(words_edit_);
    body->addSpacing(6);
    body->addLayout(listHead);
    word_list_ = new QVBoxLayout;
    word_list_->setSpacing(8);
    body->addLayout(word_list_);
    p.body->addWidget(card);

    QWidget *card2 = ui::Card();
    QVBoxLayout *b2 = CardBody(card2, 14);
    QVBoxLayout *c = FormRow(b2, "同音字");
    use_pinyin_ = new ui::Switch("同音的写法也消音（按拼音匹配，推荐）");
    use_pinyin_->setChecked(true);
    c->addWidget(use_pinyin_);
    c->addWidget(ui::Help("屏蔽了某个词，同音不同字的写法也会被消音。"));
    connect(use_pinyin_, &ui::Switch::toggled, this, [this] { ApplySettings(); SaveConfig(); });
    c = FormRow(b2, "例外是什么");
    c->addWidget(ui::Help("内置词库会屏蔽的字，出现在例外这句话里时不屏蔽。比如加了例外“打几把”，“再打几把”就不会被消音，"
                          "单说那个词仍然会。"));
    p.body->addWidget(card2);
    // Recent mutes (memory only, newest first), refreshed when the engine records a hit.
    QWidget *hitCard = ui::Card("最近消音");
    QVBoxLayout *hb = CardBody(hitCard, 6);
    auto *hits = new QListWidget;
    hits->setMinimumHeight(150);
    hits->setMaximumHeight(220);
    hb->addWidget(hits);
    hb->addWidget(ui::Help("这次打开以来最近 100 次：时间、声音来源和消掉的词。只在内存里，关掉程序就清空。"
                          "在“支持项目”页打开“把识别出的文字写进日志”时，还会带上当时识别出的整句话。漏掉的词可以在上面自己添加。"));
    auto *clearHits = ui::Pill("清空", "sec", "s");
    auto *bh2 = new QHBoxLayout;
    bh2->addWidget(clearHits);
    bh2->addStretch();
    hb->addLayout(bh2);
    auto hitSerial = std::make_shared<uint64_t>(UINT64_MAX);
    auto fillHits = [hits, hitSerial] {
        const uint64_t now = censor::HitSerial();
        if (now == *hitSerial) return;
        *hitSerial = now;
        hits->clear();
        const auto all = censor::RecentHits();
        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            QString line = QDateTime::fromMSecsSinceEpoch(it->unix_ms).toString("HH:mm:ss") + "  " +
                           Q(it->source) + "  " + Q(it->word) + (it->pinyin ? "（同音）" : "");
            if (!it->text.empty()) line += "  “" + Q(it->text) + "”";
            new QListWidgetItem(line, hits);
        }
        if (all.empty()) new QListWidgetItem("还没有消音", hits);
    };
    fillHits();
    auto *hitTimer = new QTimer(hits);
    connect(hitTimer, &QTimer::timeout, hits, fillHits);
    hitTimer->start(1000);
    connect(clearHits, &QPushButton::clicked, hits, [fillHits] {
        censor::ClearHits();
        fillHits();
    });
    p.body->addWidget(hitCard);
    p.body->addStretch();
    return p.page;
}

QWidget *LiteWindow::BuildSettingsPage() {
    PageParts p = MakePage("消音设置", "改了立即生效，过滤中也可以改");
    QWidget *card = ui::Card();
    QVBoxLayout *body = CardBody(card, 14);
    body->setSpacing(18);

    QVBoxLayout *c = FormRow(body, "替换成");
    effect_ = new ui::Segmented;
    effect_->AddOption("静音", 1);
    effect_->AddOption("哔声", 0);
    effect_->AddOption("电报音", 3);
    for (int i = 0; i < (int)std::size(kSounds); i++) effect_->AddOption(kSounds[i].name, kSoundBase + i);
    effect_->SetCurrent(0);
    c->addWidget(effect_);
    auto *ph = new QHBoxLayout;
    ph->setSpacing(10);
    preview_ = ui::Pill("▶ 试听", "sec", "s");
    connect(preview_, &QPushButton::clicked, this, &LiteWindow::PreviewEffect);
    ph->addWidget(preview_);
    effect_help_ = ui::Help("");
    ph->addWidget(effect_help_, 1);
    c->addLayout(ph);
    connect(effect_, &ui::Segmented::changed, this, [this] {
        ApplySettings();
        SaveConfig();
        UpdateDelayUi();
    });

    c = FormRow(body, "消音范围");
    padding_ = new ui::Segmented;
    padding_->AddOption("窄 0.3 秒", 300);
    padding_->AddOption("标准 0.5 秒", 500);
    padding_->AddOption("宽 0.7 秒", 700);
    padding_->SetCurrent(1);
    c->addWidget(padding_);
    c->addWidget(ui::Help("屏蔽词前后各多盖一点，免得漏出字头字尾。窄一些说话更连贯，但可能漏出半个字。"));
    connect(padding_, &ui::Segmented::changed, this, [this] {
        ApplySettings();
        SaveConfig();
        UpdateDelayUi();
    });

    c = FormRow(body, "声音延迟");
    auto *dh = new QHBoxLayout;
    dh->setSpacing(12);
    delay_auto_ = new ui::Switch("自动");
    delay_auto_->setChecked(true);
    dh->addWidget(delay_auto_);
    delay_ = new QDoubleSpinBox;
    delay_->setRange(0.6, 10.0);
    delay_->setSingleStep(0.1);
    delay_->setDecimals(1);
    delay_->setSuffix(" 秒");
    delay_->setValue(1.4);
    delay_->setFixedWidth(84);
    dh->addWidget(ui::Stepper(delay_));
    dh->addStretch();
    c->addLayout(dh);
    delay_help_ = ui::Help("");
    c->addWidget(delay_help_);
    auto delayChanged = [this] {
        ApplySettings();
        SaveConfig();
        UpdateDelayUi();
    };
    connect(delay_auto_, &ui::Switch::toggled, this, delayChanged);
    connect(delay_, &QDoubleSpinBox::editingFinished, this, delayChanged);
    p.body->addWidget(card);

    QWidget *card2 = ui::Card();
    QVBoxLayout *b2 = CardBody(card2, 14);
    b2->setSpacing(18);
    c = FormRow(b2, "VB-CABLE");
    cable_auto_ = new ui::Switch("不过滤时自动禁用 CABLE Input（推荐）");
    cable_auto_->setChecked(true);
    c->addWidget(cable_auto_);
    c->addWidget(ui::Help("开始过滤时自动启用，停止过滤或退出程序时禁用。禁用后，耳机拔掉时 Windows 不会把 CABLE 换成默认"
                          "播放设备，别的声音就不会不经过滤进入直播。推流软件用的 CABLE Output 不受影响。"));
    connect(cable_auto_, &ui::Switch::toggled, this, [this] { SaveConfig(); RefreshOutputs(); UpdateChecks(); });
    c = FormRow(b2, "开机");
    autostart_ = new ui::Switch("开机后自动缩在托盘里");
    autostart_->setChecked(AutostartEnabled());
    c->addWidget(autostart_);
    c->addWidget(ui::Help("只是打开程序，不会自动开始过滤。"));
    connect(autostart_, &ui::Switch::toggled, this, &LiteWindow::SetAutostart);
    p.body->addWidget(card2);
    p.body->addStretch();
    return p.page;
}

QWidget *LiteWindow::BuildModelPage() {
    PageParts p = MakePage("识别模型", "");
    model_meta_ = p.meta;
    QWidget *card = ui::Card();
    auto *cl = qobject_cast<QVBoxLayout *>(card->layout());
    model_list_ = new QVBoxLayout;
    model_list_->setContentsMargins(16, 4, 16, 0);
    model_list_->setSpacing(0);
    cl->addLayout(model_list_);
    model_group_ = new QButtonGroup(this);
    model_group_->setExclusive(true);
    auto *other = new QFrame;
    other->setProperty("row", "true");
    auto *oh = new QHBoxLayout(other);
    oh->setContentsMargins(16, 12, 16, 12);
    auto *ov = new QVBoxLayout;
    ov->setSpacing(2);
    auto *on = new QLabel("使用其他模型文件夹");
    on->setProperty("role", "bold");
    ov->addWidget(on);
    ov->addWidget(ui::Help("sherpa-onnx 流式模型，文件夹里要有 tokens.txt。"));
    oh->addLayout(ov, 1);
    choose_folder_ = ui::Pill("选择文件夹", "sec", "s");
    connect(choose_folder_, &QPushButton::clicked, this, &LiteWindow::ChooseModelFolder);
    oh->addWidget(choose_folder_, 0, Qt::AlignVCenter);
    cl->addWidget(other);
    p.body->addWidget(card);

    QWidget *card2 = ui::Card();
    QVBoxLayout *b2 = CardBody(card2, 14);
    b2->setSpacing(18);
    QVBoxLayout *c = FormRow(b2, "识别路数");
    lanes_ = new ui::Segmented;
    lanes_->AddOption("自动（推荐）", 0);
    lanes_->AddOption("4 路", 4);
    lanes_->AddOption("2 路", 2);
    lanes_->AddOption("1 路", 1);
    lanes_->SetCurrent(0);
    c->addWidget(lanes_);
    lanes_help_ = ui::Help("");
    c->addWidget(lanes_help_);
    UpdateLanesHelp();
    connect(lanes_, &ui::Segmented::changed, this, [this] { UpdateLanesHelp(); ApplySettings(); SaveConfig(); });
    c = FormRow(b2, "消音位置校准");
    offset_ = new QSpinBox;
    offset_->setRange(-1000, 1000);
    offset_->setSingleStep(50);
    offset_->setSuffix(" 毫秒");
    offset_->setFixedWidth(100);
    c->addWidget(ui::Stepper(offset_));
    c->addWidget(ui::Help("消音总是比屏蔽词早或晚时再调，负数往前、正数往后。一般不用改。"));
    connect(offset_, &QSpinBox::editingFinished, this, [this] { ApplySettings(); SaveConfig(); });
    p.body->addWidget(card2);
    p.body->addWidget(ui::Help("下载来源 ModelScope。下载的模型存在 " + QDir::toNativeSeparators(DownloadDir()) + "。"));
    p.body->addStretch();
    return p.page;
}

QWidget *LiteWindow::BuildCheckPage() {
    PageParts p = MakePage("开播前检查",
                           "每 2 秒刷新。只在你点按钮时才改 Windows 设置或安装东西，不改推流软件。有红色项时点“开始过滤”会先问你。");
    QWidget *card = ui::Card();
    check_list_ = qobject_cast<QVBoxLayout *>(card->layout());
    check_list_->setContentsMargins(0, 4, 0, 4);
    p.body->addWidget(card);
    p.body->addStretch();
    return p.page;
}

QWidget *LiteWindow::BuildTitleBar() {
    title_bar_ = new QWidget;
    title_bar_->setObjectName("titlebar");
    title_bar_->setAttribute(Qt::WA_StyledBackground);
    title_bar_->setFixedHeight(36);
    auto *h = new QHBoxLayout(title_bar_);
    h->setContentsMargins(14, 0, 0, 0);
    h->setSpacing(8);
    auto *icon = new QLabel;
    icon->setPixmap(ui::AppIcon().pixmap(QSize(16, 16), devicePixelRatioF()));
    h->addWidget(icon);
    auto *title = new QLabel("直播消音");
    title->setProperty("role", "meta");
    h->addWidget(title);
    h->addStretch();
    auto *min = new ui::WindowButton(ui::WindowButton::Minimize);
    max_button_ = new ui::WindowButton(ui::WindowButton::Maximize);
    auto *close = new ui::WindowButton(ui::WindowButton::Close);
    min->setToolTip("最小化");
    max_button_->setToolTip("最大化");
    min->setAccessibleName("最小化");
    max_button_->setAccessibleName("最大化");
    close->setAccessibleName("关闭");
    close->setToolTip("关闭（程序留在托盘里）");
    connect(min, &QAbstractButton::clicked, this, &QWidget::showMinimized);
    connect(max_button_, &QAbstractButton::clicked, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
    connect(close, &QAbstractButton::clicked, this, &QWidget::close);
    h->addWidget(min);
    h->addWidget(max_button_);
    h->addWidget(close);
    return title_bar_;
}

QWidget *LiteWindow::BuildSupportPage() {
    PageParts p = MakePage("支持项目", "作者：漠秋　反馈 QQ：549466926<br>"
                                       "项目地址：<a href=\"https://github.com/Mochall/live-speech-filter\">https://github.com/Mochall/live-speech-filter</a><br>"
                                       "本程序免费使用，如果有帮助到你，可以选择支持项目。");
    p.meta->setTextFormat(Qt::RichText);
    p.meta->setOpenExternalLinks(true);
    p.meta->setTextInteractionFlags(Qt::TextBrowserInteraction);
    QWidget *card = ui::Card();
    QVBoxLayout *body = CardBody(card, 16);
    // The QR image is compiled into the program when the build sets LSF_SPONSOR_IMAGE (as in the
    // plugin), so nobody can swap it by replacing a file.
    QPixmap qr(":/sponsor/sponsor-qr.png");
    auto *image = new QLabel;
    image->setAlignment(Qt::AlignCenter);
    if (!qr.isNull()) {
        const qreal dpr = devicePixelRatioF();
        qr = qr.scaled(QSize(280, 280) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        qr.setDevicePixelRatio(dpr);
        image->setPixmap(qr);
    } else {
        image->setText("（这个版本没有附带赞助二维码）");
        image->setProperty("role", "meta");
        image->setMinimumHeight(120);
    }
    body->addWidget(image);
    p.body->addWidget(card);

    QWidget *logCard = ui::Card("日志");
    QVBoxLayout *lb = CardBody(logCard, 6);
    QVBoxLayout *c = FormRow(lb, "排查漏词");
    debug_log_ = new ui::Switch("把识别出的文字写进日志");
    c->addWidget(debug_log_);
    c->addWidget(ui::Help("只在排查漏词或误消音时打开。日志只存在本机，里面会有你说的话。"));
    connect(debug_log_, &ui::Switch::toggled, this, [this] { ApplySettings(); SaveConfig(); });
    auto *bh = new QHBoxLayout;
    bh->setSpacing(8);
    auto *openLog = ui::Pill("打开日志", "sec", "s");
    auto *openDir = ui::Pill("打开日志文件夹", "sec", "s");
    connect(openLog, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(paths_.log_path)); });
    connect(openDir, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(paths_.config_dir + "/logs")); });
    bh->addWidget(openLog);
    bh->addWidget(openDir);
    bh->addStretch();
    c->addLayout(bh);

    p.body->addWidget(logCard);

    QWidget *aboutCard = ui::Card("关于");
    QVBoxLayout *ab = CardBody(aboutCard, 6);
    ab->setSpacing(14);
    c = FormRow(ab, "程序");
    c->addWidget(ui::Help("直播消音 " LSF_VERSION "。说的话在本机识别，不上传；只有下载模型时会联网（ModelScope）。"));
    c = FormRow(ab, "许可");
    c->addWidget(ui::Help("基于 Cloud370/obs-profanity-filter（GPL-2.0）修改。源代码与第三方许可见程序文件夹里的 licenses。"));
    p.body->addWidget(aboutCard);
    p.body->addStretch();
    return p.page;
}

void LiteWindow::BuildTray() {
    popup_ = new TrayPopup;
    connect(popup_, &TrayPopup::startStop, this, &LiteWindow::ToggleFilter);
    connect(popup_, &TrayPopup::openMain, this, [this] { ShowPage(kFilter); });
    connect(popup_, &TrayPopup::openSettings, this, [this] { ShowPage(kSettings); });
    connect(popup_, &TrayPopup::noteAction, this, [this] { ShowPage(CurrentStatus().note_page); });

    tray_ = new QSystemTrayIcon(ui::TrayIcon(Tone::Off), this);
    tray_->setToolTip("直播消音：未在过滤");
    auto *menu = new QMenu(this);
    menu->addAction("打开主窗口", this, [this] { ShowPage(kFilter); });
    auto *toggle = menu->addAction("开始过滤", this, &LiteWindow::ToggleFilter);
    toggle->setObjectName("toggle");
    menu->addSeparator();
    menu->addAction("退出（停止输出）", this, [this] {
        quitting_ = true;
        close();
    });
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger) {
            if (popup_->isVisible()) popup_->hide();
            else {
                UpdateStatus();
                popup_->ShowNear(tray_->geometry());
            }
        } else if (r == QSystemTrayIcon::DoubleClick) {
            popup_->hide();
            ShowPage(kFilter);
        }
    });
    tray_->show();
}

void LiteWindow::ShowPage(Page page) {
    nav_->button(page)->setChecked(true);
    pages_->setCurrentIndex(page);
    showNormal();
    raise();
    activateWindow();
}

// ============================================================================= devices and config

// "CABLE Input (VB-Audio Virtual Cable)" -> "虚拟声卡 CABLE Input（推荐）". The Windows name is kept in
// kRealName for the config and the checks.
static constexpr int kRealName = Qt::UserRole + 1;

// The cable's stereo playback endpoint is shown as "CABLE Input" whatever Windows calls it: after a
// fresh install Chinese Windows names it "扬声器 (VB-Audio Virtual Cable)".
static QString CableShortName(const QString &name) {
    if (IsCableEndpointName(name.toStdString()) && !IsCable16chEndpointName(name.toStdString())) return "CABLE Input";
    const int paren = name.indexOf(" (");
    return paren > 0 ? name.left(paren) : name;
}

// The cable's recording endpoint as Windows shows it, for "选 CABLE Output" hints: on some machines
// it is not called "CABLE Output" ("麦克风 (VB-Audio Virtual Cable)").
static QString CableRecordingName() {
    for (auto &e : ListEndpoints(1, true))
        if (IsCableEndpointName(e.name)) {
            const QString name = Q(e.name);
            return name.startsWith("CABLE Output") ? QString("CABLE Output") : "CABLE Output（这台电脑上叫“" + name + "”）";
        }
    return "CABLE Output";
}

// auto_enable: the program enables the cable itself when filtering starts, so "disabled" is normal.
static void AddOutputItem(QComboBox *box, const Endpoint &e, bool auto_enable) {
    const QString name = Q(e.name);
    const QString shortName = CableShortName(name);
    QString label = "虚拟声卡 " + shortName;
    QString note;
    if (shortName == "CABLE Input") note = "推荐";
    if (e.state == DEVICE_STATE_DISABLED)
        note += QString(note.isEmpty() ? "" : "，") + (auto_enable ? "开始过滤时自动启用" : "已禁用");
    else if (e.state != DEVICE_STATE_ACTIVE) note += QString(note.isEmpty() ? "" : "，") + "不可用";
    if (!note.isEmpty()) label += "（" + note + "）";
    box->addItem(label, Q(Utf8(e.id)));
    box->setItemData(box->count() - 1, name, kRealName);
    box->setItemData(box->count() - 1, name, Qt::ToolTipRole);
}

// The cable's playback endpoints offered as output. CABLE In 16ch is the same cable as CABLE Input
// (both arrive at CABLE Output); it is listed only when CABLE Input is missing, so there is no choice to
// puzzle over.
static std::vector<Endpoint> CableOutputs() {
    std::vector<Endpoint> all;
    bool has_input = false;
    for (auto &e : ListEndpoints(0, true)) {
        if (!IsCableEndpointName(e.name)) continue;
        if (!IsCable16chEndpointName(e.name)) has_input = true;
        all.push_back(e);
    }
    std::vector<Endpoint> out;
    for (auto &e : all)
        if (!has_input || !IsCable16chEndpointName(e.name)) out.push_back(e);
    return out;
}

// "自动" lanes: 4 on a machine with 12 or more logical cores, otherwise 2. 4 lanes cost about
// 3 cores per filtered source (0.8 each), which a 6-core/12-thread CPU can spare next to a game;
// 1 lane misses words (average 7.75/9, worst 6/9 in the file test), so auto never picks it.
static int AutoLanes() {
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return n >= 12 ? 4 : 2;
}

void LiteWindow::FillDevices() {
    Row *m = rows_[0].get();
    m->device->clear();
    for (auto &e : ListEndpoints(1)) {
        if (IsCableEndpointName(e.name)) continue; // the cable is the output, not a microphone
        m->device->addItem(Q(e.name) + (e.is_default ? "（默认）" : ""), Q(Utf8(e.id)));
    }
    output_->clear();
    for (auto &e : CableOutputs()) {
        AddOutputItem(output_, e, cable_auto_ && cable_auto_->isChecked());
        if (!IsCable16chEndpointName(e.name)) output_->setCurrentIndex(output_->count() - 1);
    }
}

void LiteWindow::LoadConfig() {
    QJsonObject o = QJsonDocument::fromJson(ReadText(paths_.config_dir + "/config.json").toUtf8()).object();
    const bool first = o.isEmpty();
    Row *m = rows_[0].get();
    const QSignalBlocker b1(m->check), b2(m->device), b3(output_), b5(delay_auto_), b6(delay_), b8(use_pinyin_),
        b9(offset_), b10(debug_log_), b11(cable_auto_);
    m->check->setChecked(o.value("use_mic").toBool(true));
    QString mic = o.value("mic_name").toString();
    for (int i = 0; i < m->device->count(); i++) {
        QString t = m->device->itemText(i);
        if (mic.isEmpty() ? t.endsWith("（默认）") : t == mic || t == mic + "（默认）") {
            m->device->setCurrentIndex(i);
            break;
        }
    }
    QString out = o.value("output_name").toString();
    for (int i = 0; !out.isEmpty() && i < output_->count(); i++)
        if (output_->itemData(i, kRealName).toString() == out) output_->setCurrentIndex(i);
    QJsonArray apps = first ? QJsonArray{QJsonObject{{"exe", "KOOK.exe"}, {"on", true}}} : o.value("apps").toArray();
    for (const auto &a : apps) AddProgramRow(a.toObject().value("exe").toString(), a.toObject().value("on").toBool(true));
    const int pad = o.value("padding_ms").toInt(500);
    int pi = padding_->FindData(pad);
    if (pi < 0) {
        padding_->AddOption(QString("自定义 %1 秒").arg(pad / 1000.0), pad);
        pi = padding_->count() - 1;
    }
    padding_->SetCurrent(pi);
    const int fx = effect_->FindData(o.value("effect").toInt(1));
    effect_->SetCurrent(fx < 0 ? 0 : fx);
    delay_auto_->setChecked(o.value("delay_auto").toBool(true));
    delay_->setValue(o.value("delay_s").toDouble(1.4));
    const int li = lanes_->FindData(o.value("lanes").toInt(0));
    lanes_->SetCurrent(li < 0 ? 0 : li);
    UpdateLanesHelp();
    use_pinyin_->setChecked(o.value("use_pinyin").toBool(true));
    offset_->setValue(o.value("model_offset_ms").toInt(0));
    debug_log_->setChecked(o.value("debug_logging").toBool(false));
    cable_auto_->setChecked(o.value("cable_auto").toBool(true));
    model_path_ = o.value("model_path").toString();
    custom_model_ = o.value("custom_model").toString();
}

void LiteWindow::UpdateLanesHelp() {
    const QString base = "同一路声音同时用几份识别错开跑，越多越不容易漏词。每路约占 0.8 个 CPU 核；"
                         "识别跟不上时减少。过滤中也可以改。";
    if (lanes_->currentData().toInt() == 0)
        lanes_help_->setText(QString("这台电脑有 %1 个逻辑核，自动用 %2 路（12 个及以上用 4 路，否则 2 路）。")
                                 .arg(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))
                                 .arg(AutoLanes()) +
                             base);
    else
        lanes_help_->setText(base);
}

int LiteWindow::EffectiveLanes() const {
    const int n = lanes_->currentData().toInt();
    return n > 0 ? n : AutoLanes();
}

QString LiteWindow::OutputName() const { return output_->currentData(kRealName).toString(); }

QString LiteWindow::ShortOutputName() const { return CableShortName(OutputName()); }

void LiteWindow::SaveConfig() {
    if (loading_) return;
    QJsonObject o;
    Row *m = rows_[0].get();
    o["use_mic"] = m->check->isChecked();
    QString mic = m->device->currentText();
    if (mic.endsWith("（默认）")) mic.chop(4);
    o["mic_name"] = mic;
    o["output_name"] = OutputName();
    QJsonArray apps;
    for (auto &r : rows_)
        if (!r->is_mic) apps.append(QJsonObject{{"exe", r->exe}, {"on", r->check->isChecked()}});
    o["apps"] = apps;
    o["padding_ms"] = padding_->currentData().toInt();
    o["effect"] = effect_->currentData().toInt();
    o["delay_auto"] = delay_auto_->isChecked();
    o["delay_s"] = delay_->value();
    o["lanes"] = lanes_->currentData().toInt();
    o["use_pinyin"] = use_pinyin_->isChecked();
    o["model_offset_ms"] = offset_->value();
    o["debug_logging"] = debug_log_->isChecked();
    o["cable_auto"] = cable_auto_->isChecked();
    o["model_path"] = model_path_;
    o["custom_model"] = custom_model_;
    QString error;
    if (!WriteText(paths_.config_dir + "/config.json", QString::fromUtf8(QJsonDocument(o).toJson()), error))
        CLOG(censor::kLogWarning, "[Lite] cannot save settings: %s", error.toUtf8().constData());
}

QString LiteWindow::ModelPath() const {
    const QString p = paths_.model_dir.isEmpty() ? model_path_ : paths_.model_dir;
    return !p.isEmpty() && QFile::exists(p + "/tokens.txt") ? p : QString();
}

void LiteWindow::ApplySettings() {
    AppSettings s;
    s.model_path = QDir::toNativeSeparators(ModelPath()).toStdString();
    s.censor_padding_ms = padding_->currentData().toInt();
    const int fx = effect_->currentData().toInt();
    s.effect = fx;
    if (fx >= kSoundBase && fx - kSoundBase < (int)clips_.size()) {
        s.effect = 4;
        s.clip = clips_[fx - kSoundBase]; // missing file: the engine mutes
        if (s.clip) s.clip_period = s.clip->size() + kSounds[fx - kSoundBase].gap_ms * 48;
    }
    const double min_delay = s.censor_padding_ms / 1000.0 + AppSettings::kMinDelayMarginMs / 1000.0;
    const double auto_delay = s.censor_padding_ms / 1000.0 + AppSettings::kAutoDelayMarginMs / 1000.0;
    {
        const QSignalBlocker b(delay_);
        delay_->setMinimum(min_delay);
        if (delay_auto_->isChecked()) delay_->setValue(auto_delay);
    }
    delay_->parentWidget()->setEnabled(!delay_auto_->isChecked()); // box and its −/+ buttons
    s.delay_seconds = delay_auto_->isChecked() ? auto_delay : std::max(delay_->value(), min_delay);
    s.recognizer_lanes = EffectiveLanes();
    s.lane_step_ms = s.recognizer_lanes >= 4 ? 40 : s.recognizer_lanes == 3 ? 53 : 80;
    s.model_offset_ms = offset_->value();
    s.debug_logging = debug_log_->isChecked();
    s.use_pinyin = use_pinyin_->isChecked();
    s.dict_dir = std::filesystem::path(paths_.exe_dir.toStdWString()) / "data" / "dict";
    s.builtin_words = ReadText(paths_.exe_dir + "/data/builtin_dirty_words.txt").toStdString();
    QString custom = ReadText(paths_.config_dir + "/custom_words.txt");
    s.custom_words = custom.toStdString();
    s.pinyin_words = ReadText(paths_.config_dir + "/pinyin_words.txt").toStdString();
    settings_.Set(s);
    const int n = WordLines(custom).size();
    words_count_->setText(n ? QString::number(n) : QString());
}

void LiteWindow::UpdateDelayUi() {
    const int pad = padding_->currentData().toInt();
    const double d = delay_->value();
    if (delay_auto_->isChecked())
        delay_help_->setText(QString("自动＝消音范围加 0.9 秒，现在是 %1 秒。观众听到的声音比你说话晚这么多，画面不受影响。")
                                 .arg(d, 0, 'f', 1));
    else
        delay_help_->setText(QString("手动，最少 %1 秒（消音范围加 0.7 秒）。越长越不容易漏，观众听到的越晚。"
                                     "直播回放里听到屏蔽词的字头漏出来，就调长一点。")
                                 .arg(pad / 1000.0 + AppSettings::kMinDelayMarginMs / 1000.0, 0, 'f', 1));
    const int fx = effect_->currentData().toInt();
    const bool sound = fx >= kSoundBase;
    const bool missing = sound && !clips_[fx - kSoundBase];
    const QString help = sound ? QString(kSounds[fx - kSoundBase].help) : QString(kEffectHelp[std::clamp(fx, 0, 3)]);
    effect_help_->setText(missing ? QString("程序文件夹 data\\sounds 里缺这个声音文件，现在按静音处理。")
                                  : help + (fx == 1 ? "" : "试听从你的耳机播放，不会送进直播。"));
    preview_->setEnabled(fx != 1 && !missing);
}

void LiteWindow::PreviewEffect() {
    PlaySoundW(nullptr, nullptr, 0); // stop a preview still playing before its buffer is replaced
    const int fx = effect_->currentData().toInt();
    if (fx >= kSoundBase) {
        const auto &clip = clips_[fx - kSoundBase];
        if (!clip) return;
        preview_wav_ = ClipWav(*clip, clip->size() + kSounds[fx - kSoundBase].gap_ms * 48);
    } else {
        preview_wav_ = EffectWav(fx);
    }
    PlaySoundW(reinterpret_cast<LPCWSTR>(preview_wav_.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

bool LiteWindow::AutostartEnabled() const {
    QSettings run(kRunKey, QSettings::NativeFormat);
    return run.contains(kRunName);
}

void LiteWindow::SetAutostart(bool on) {
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (on)
        run.setValue(kRunName, "\"" + QDir::toNativeSeparators(QApplication::applicationFilePath()) + "\" --tray");
    else
        run.remove(kRunName);
    run.sync();
    CLOG(censor::kLogInfo, "[Lite] start with Windows: %s", on ? "on" : "off");
}

// ============================================================================= models

void LiteWindow::ScanModels() {
    ClearLayout(model_list_);
    for (auto &e : models_)
        if (e.radio) model_group_->removeButton(e.radio);
    models_.clear();
    download_label_ = nullptr;
    const bool running = pipeline_.running();
    auto local = [](const QString &path) { return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()); };

    // The catalog first, installed or not; then other model folders that were found or chosen.
    for (const auto &k : kCatalog) {
        ModelEntry e;
        e.id = k.id;
        e.name = k.name;
        e.size = k.size;
        e.note = k.note;
        e.url = k.url;
        const QString bundled = paths_.exe_dir + "/models/" + e.id, downloaded = DownloadDir() + "/" + e.id;
        if (QFile::exists(bundled + "/tokens.txt")) {
            e.path = local(bundled);
            e.bundled = true;
            e.size += "，随程序安装";
        } else if (QFile::exists(downloaded + "/tokens.txt")) {
            e.path = local(downloaded);
            e.downloaded = true;
        } else if (e.url.isEmpty()) {
            // The OBS plugin installs the same recommended model; use its copy instead of a second one.
            QDir plugins(qEnvironmentVariable("ProgramData", "C:/ProgramData") + "/obs-studio/plugins");
            for (const QString &plugin : plugins.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString dir = plugins.filePath(plugin + "/data/models/" + e.id);
                if (QFile::exists(dir + "/tokens.txt")) {
                    e.path = local(dir);
                    e.size += "，用 OBS 插件里装的";
                    break;
                }
            }
        }
        models_.push_back(e);
    }
    auto addDir = [&](const QString &path, const QString &note) {
        if (!QFile::exists(path + "/tokens.txt")) return;
        for (auto &e : models_)
            if (!e.path.isEmpty() && QFileInfo(e.path) == QFileInfo(path)) return;
        for (auto &e : models_)
            if (e.path.isEmpty() && QFileInfo(path).fileName() == e.id) { // a catalog model kept somewhere else
                e.path = local(path);
                return;
            }
        ModelEntry e;
        e.path = local(path);
        e.name = QFileInfo(path).fileName();
        e.note = note;
        e.size = FolderSize(path);
        models_.push_back(e);
    };
    if (!paths_.model_dir.isEmpty()) addDir(paths_.model_dir, "命令行 --model 指定的文件夹：" + paths_.model_dir);
    if (!custom_model_.isEmpty()) addDir(custom_model_, "其他文件夹：" + custom_model_);
    for (const QString &dir : {paths_.exe_dir + "/models", DownloadDir()}) {
        QDir d(dir);
        for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            if (!sub.endsWith("_temp")) addDir(d.filePath(sub), "sherpa-onnx 流式模型：" + local(d.filePath(sub)));
    }

    // Keep the saved choice; otherwise the first installed one (the catalog starts with 推荐).
    int chosen = -1;
    const QString want = paths_.model_dir.isEmpty() ? model_path_ : paths_.model_dir;
    for (size_t i = 0; i < models_.size(); i++)
        if (!want.isEmpty() && !models_[i].path.isEmpty() && QFileInfo(models_[i].path) == QFileInfo(want)) chosen = (int)i;
    for (size_t i = 0; chosen < 0 && i < models_.size(); i++)
        if (!models_[i].path.isEmpty()) chosen = (int)i;
    if (paths_.model_dir.isEmpty()) model_path_ = chosen >= 0 ? models_[chosen].path : QString();

    for (size_t i = 0; i < models_.size(); i++) {
        ModelEntry &e = models_[i];
        const bool installed = !e.path.isEmpty(), in_use = (int)i == chosen;
        auto *row = new QFrame;
        row->setProperty("row", i == 0 ? "first" : "true");
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 12, 0, 12);
        h->setSpacing(10);
        auto *v = new QVBoxLayout;
        v->setSpacing(2);
        auto *top = new QHBoxLayout;
        top->setSpacing(8);
        e.radio = new QRadioButton;
        e.radio->setStyleSheet("font-weight:700");
        {
            // A folder name can be very long (sherpa-onnx-...-2026-06-05) and would widen the page.
            QFont bold = e.radio->font();
            bold.setBold(true);
            e.radio->setText(QFontMetrics(bold).elidedText(e.name, Qt::ElideMiddle, 360));
        }
        e.radio->setChecked(in_use);
        e.radio->setEnabled(installed && !running);
        e.radio->setToolTip(e.path.isEmpty() ? e.name : e.name + "\n" + e.path);
        top->addWidget(e.radio);
        auto *size = new QLabel(e.size);
        size->setProperty("role", "meta");
        top->addWidget(size);
        top->addStretch();
        v->addLayout(top);
        QString note = e.note;
        if (!installed && e.url.isEmpty()) note += "程序文件夹里没有这个模型，重新安装程序可以恢复。";
        auto *n = ui::Help(note);
        n->setContentsMargins(24, 0, 0, 0);
        v->addWidget(n);
        h->addLayout(v, 1);
        e.chip = ui::MakeChip();
        ui::SetChip(e.chip, Tone::Ok, "正在使用");
        e.chip->setVisible(in_use);
        h->addWidget(e.chip, 0, Qt::AlignVCenter);
        const QString id = e.id, url = e.url, path = e.path;
        if (!id.isEmpty() && download_ && download_id_ == id) {
            download_label_ = ui::Help(download_text_);
            h->addWidget(download_label_, 0, Qt::AlignVCenter);
            auto *cancel = ui::Pill("取消", "sec", "s");
            connect(cancel, &QPushButton::clicked, this, [this] {
                if (download_) download_->abort();
            });
            h->addWidget(cancel, 0, Qt::AlignVCenter);
        } else if (!installed && !url.isEmpty()) {
            auto *get = ui::Pill("下载", "sec", "s");
            get->setEnabled(!download_);
            get->setToolTip(download_ ? "等正在下载的模型下完" : "从 ModelScope 下载，约 " + e.size);
            connect(get, &QPushButton::clicked, this, [this, id, url] { DownloadModel(id, url); });
            h->addWidget(get, 0, Qt::AlignVCenter);
        } else if (e.downloaded) {
            auto *del = ui::Pill("删除", "sec", "s");
            del->setEnabled(!(running && in_use));
            del->setToolTip(running && in_use ? "正在使用，先停止过滤" : "删除下载的模型文件，以后可以重新下载");
            connect(del, &QPushButton::clicked, this, [this, id, path] { DeleteModel(id, path); });
            h->addWidget(del, 0, Qt::AlignVCenter);
        }
        model_group_->addButton(e.radio, (int)i);
        model_list_->addWidget(row);
        connect(e.radio, &QRadioButton::toggled, this, [this, i](bool on) {
            if (!on) return;
            model_path_ = models_[i].path;
            for (size_t k = 0; k < models_.size(); k++) models_[k].chip->setVisible(k == i);
            ApplySettings();
            SaveConfig();
            UpdateChecks();
        });
    }
}

void LiteWindow::DownloadModel(const QString &id, const QString &url) {
    if (download_) return;
    QDir().mkpath(DownloadDir());
    download_file_ = new QFile(DownloadDir() + "/" + id + ".zip.part", this);
    if (!download_file_->open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "无法下载", "不能在模型文件夹里写文件：\n" + QDir::toNativeSeparators(DownloadDir()));
        delete download_file_;
        download_file_ = nullptr;
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    download_ = net_->get(request);
    download_id_ = id;
    download_text_ = "正在连接…";
    CLOG(censor::kLogInfo, "[Lite] model download started: %s", url.toUtf8().constData());
    connect(download_, &QNetworkReply::readyRead, this, [this] { download_file_->write(download_->readAll()); });
    connect(download_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        download_text_ = total > 0 ? QString("正在下载 %1%").arg(got * 100 / total)
                                   : QString("正在下载 %1 MB").arg(got / (1024 * 1024));
        if (download_label_) download_label_->setText(download_text_);
    });
    connect(download_, &QNetworkReply::finished, this, &LiteWindow::DownloadFinished);
    ScanModels();
}

void LiteWindow::DownloadFinished() {
    QNetworkReply *reply = download_;
    const QString id = download_id_;
    download_file_->write(reply->readAll());
    download_file_->close();
    const QString part = download_file_->fileName(), zip = DownloadDir() + "/" + id + ".zip";
    download_file_->deleteLater();
    download_file_ = nullptr;
    const auto error = reply->error();
    const QString error_text = reply->errorString();
    reply->deleteLater();
    if (error != QNetworkReply::NoError) {
        download_ = nullptr;
        QFile::remove(part);
        const bool cancelled = error == QNetworkReply::OperationCanceledError;
        CLOG(censor::kLogWarning, "[Lite] model download %s: %s", id.toUtf8().constData(),
             cancelled ? "cancelled" : error_text.toUtf8().constData());
        if (!quitting_) ScanModels();
        if (!cancelled && !quitting_) QMessageBox::warning(this, "下载失败", "模型没有下载成功，可以稍后再试。\n" + error_text);
        return;
    }
    QFile::remove(zip);
    QFile::rename(part, zip);
    InstallDownloaded(id, zip);
}

void LiteWindow::InstallDownloaded(const QString &id, const QString &zip) {
    // Windows 10 1803+ ships tar.exe (libarchive), which reads zip files. It runs inside a temporary
    // folder next to the zip with relative names, so the user's path never passes through it.
    download_text_ = "正在解压…";
    if (download_label_) download_label_->setText(download_text_);
    const QString temp = id + "_temp";
    QDir models(DownloadDir());
    QDir(models.filePath(temp)).removeRecursively();
    models.mkpath(temp);
    auto *tar = new QProcess(this);
    tar->setWorkingDirectory(models.filePath(temp));
    auto done = [this, tar, id, zip, temp](bool started_ok) {
        tar->deleteLater();
        QDir models(DownloadDir());
        QString root;
        QDirIterator it(models.filePath(temp), {"tokens.txt"}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) root = QFileInfo(it.next()).absolutePath();
        const QString final_dir = models.filePath(id);
        bool ok = started_ok && !root.isEmpty();
        if (ok) {
            QDir(final_dir).removeRecursively();
            ok = QDir().rename(root, final_dir);
        }
        QDir(models.filePath(temp)).removeRecursively();
        QFile::remove(zip);
        download_ = nullptr;
        CLOG(ok ? censor::kLogInfo : censor::kLogWarning, "[Lite] model %s %s", id.toUtf8().constData(),
             ok ? "installed" : "could not be unpacked");
        if (ok && !pipeline_.running()) model_path_ = QDir::toNativeSeparators(final_dir); // use what was just downloaded
        ScanModels();
        ApplySettings();
        SaveConfig();
        UpdateChecks();
        if (!ok) QMessageBox::warning(this, "安装失败", "模型下载了，但没能解压，或者里面没有 tokens.txt。可以稍后重新下载。");
    };
    connect(tar, &QProcess::finished, this,
            [done](int code, QProcess::ExitStatus status) { done(status == QProcess::NormalExit && code == 0); });
    connect(tar, &QProcess::errorOccurred, this, [done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) done(false);
    });
    tar->start(qEnvironmentVariable("SystemRoot", "C:\\Windows") + "\\System32\\tar.exe", {"-xf", "../" + id + ".zip"});
}

void LiteWindow::DeleteModel(const QString &id, const QString &path) {
    QString name = id;
    for (auto &e : models_)
        if (e.id == id) name = e.name;
    if (QMessageBox::question(this, "删除模型", "删除下载的“" + name + "”模型？以后要用可以重新下载。") != QMessageBox::Yes) return;
    if (!QDir(path).removeRecursively()) QMessageBox::warning(this, "删除失败", "有文件没删掉，可能正被使用：\n" + path);
    if (QFileInfo(model_path_) == QFileInfo(path)) model_path_.clear();
    CLOG(censor::kLogInfo, "[Lite] model %s deleted", id.toUtf8().constData());
    ScanModels();
    ApplySettings();
    SaveConfig();
    UpdateChecks();
}

void LiteWindow::ChooseModelFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, "选择模型文件夹", custom_model_);
    if (dir.isEmpty()) return;
    if (!QFile::exists(dir + "/tokens.txt")) {
        QMessageBox::warning(this, "不是模型文件夹", "这个文件夹里没有 tokens.txt，不是 sherpa-onnx 流式模型。");
        return;
    }
    custom_model_ = QDir::toNativeSeparators(dir);
    model_path_ = custom_model_;
    ScanModels();
    ApplySettings();
    SaveConfig();
    UpdateChecks();
}

// ============================================================================= words

QStringList LiteWindow::ReadWords() const { return WordLines(ReadText(paths_.config_dir + "/custom_words.txt")); }

bool LiteWindow::WriteWords(const QStringList &words) {
    QString error;
    if (!WriteText(paths_.config_dir + "/custom_words.txt", words.join("\n"), error)) {
        QMessageBox::warning(this, "无法保存", "写入失败，屏蔽词没有改变。\n" + error);
        return false;
    }
    ApplySettings();
    RebuildWordList();
    return true;
}

void LiteWindow::AddWord() {
    QString w = word_edit_->text().trimmed();
    w.remove(QRegularExpression(R"(^!+)"));
    w.remove(QRegularExpression(R"([,，、\r\n])"));
    if (w.isEmpty()) return;
    const QString entry = (word_kind_->currentData().toBool() ? "!" : "") + w;
    QStringList words = ReadWords();
    if (!words.contains(entry)) {
        words << entry;
        if (!WriteWords(words)) return;
    }
    word_edit_->clear();
}

void LiteWindow::RebuildWordList() {
    ClearLayout(word_list_);
    const QStringList words = ReadWords();
    if (words.isEmpty()) editing_words_ = false;
    words_edit_->setVisible(!words.isEmpty());
    words_edit_->setText(editing_words_ ? "完成" : "编辑");
    if (words.isEmpty()) {
        word_list_->addWidget(ui::Help("还没有自己加的词。"));
        return;
    }
    // Blocked words, then allowed phrases, each as wrapping tags.
    for (const bool allow : {false, true}) {
        QStringList group;
        for (const QString &w : words)
            if (w.startsWith('!') == allow) group << w;
        if (group.isEmpty()) continue;
        word_list_->addWidget(ui::Help(allow ? "例外，不屏蔽" : "屏蔽"));
        auto *box = new QWidget;
        auto *flow = new ui::FlowLayout(box, 6, 4);
        for (const QString &entry : group) {
            auto *chip = new ui::WordChip(allow ? entry.mid(1) : entry, allow);
            chip->SetEditing(editing_words_);
            connect(chip, &ui::WordChip::removeClicked, this, [this, entry] {
                QStringList list = ReadWords();
                list.removeAll(entry);
                WriteWords(list);
            });
            flow->addWidget(chip);
        }
        word_list_->addWidget(box);
    }
    if (editing_words_) word_list_->addWidget(ui::Help("点词右上角的 × 删除，删完点“完成”。"));
}

// ============================================================================= programs

LiteWindow::Row *LiteWindow::AddProgramRow(const QString &exe, bool checked) {
    if (exe.isEmpty()) return nullptr;
    for (auto &r : rows_)
        if (!r->is_mic && r->exe.compare(exe, Qt::CaseInsensitive) == 0) return nullptr;
    auto row = std::make_unique<Row>();
    Row *r = row.get();
    r->exe = exe;
    r->frame = new QFrame;
    r->frame->setProperty("row", "true");
    auto *v = new QVBoxLayout(r->frame);
    v->setContentsMargins(0, 10, 0, 10);
    v->setSpacing(6);
    auto *h = new QHBoxLayout;
    h->setSpacing(10);
    r->check = new QCheckBox(exe);
    r->check->setChecked(checked);
    h->addWidget(r->check);
    r->remove = ui::Link("移除");
    r->remove->setStyleSheet("font-size:12px");
    h->addWidget(r->remove);
    h->addStretch();
    r->state = new QLabel;
    r->state->setProperty("role", "state");
    h->addWidget(r->state);
    v->addLayout(h);
    r->track = new ui::TrackView;
    r->track->setFixedHeight(52);
    v->addWidget(r->track);
    source_rows_->addWidget(r->frame);
    connect(r->check, &QCheckBox::toggled, this, [this] { SaveConfig(); UpdateChecks(); });
    connect(r->remove, &QPushButton::clicked, this, [this, r] { RemoveProgramRow(r); });
    rows_.push_back(std::move(row));
    r->remove->setVisible(!pipeline_.running());
    r->check->setEnabled(!pipeline_.running());
    return r;
}

void LiteWindow::RemoveProgramRow(Row *row) {
    for (auto it = rows_.begin(); it != rows_.end(); ++it) {
        if (it->get() != row) continue;
        source_rows_->removeWidget(row->frame);
        row->frame->hide();
        row->frame->deleteLater();
        rows_.erase(it);
        break;
    }
    SaveConfig();
    UpdateChecks();
}

void LiteWindow::ShowAddProgramsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("添加要过滤的程序");
    dialog.resize(440, 440);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);
    auto *title = new QLabel("添加要过滤的程序");
    title->setObjectName("cardTitle");
    layout->addWidget(title);
    layout->addWidget(ui::Help("可以同时勾选多个。列表里是现在有声音的程序；还没打开的，在下面输入文件名。"));
    auto *list = new QListWidget;
    auto programs = AudioPrograms();
    std::sort(programs.begin(), programs.end(), [](const AudioProgram &a, const AudioProgram &b) {
        return a.active != b.active ? a.active : a.exe < b.exe;
    });
    for (auto &p : programs) {
        QString exe = Q(p.exe);
        bool present = false;
        for (auto &r : rows_) present |= !r->is_mic && r->exe.compare(exe, Qt::CaseInsensitive) == 0;
        auto *item = new QListWidgetItem(exe + (present ? "（已添加）" : p.active ? "（正在出声）" : ""), list);
        item->setData(Qt::UserRole, exe);
        item->setFlags(present ? Qt::NoItemFlags : Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(present ? Qt::Checked : Qt::Unchecked);
    }
    if (programs.empty()) new QListWidgetItem("（现在没有程序在播放声音）", list);
    // A click anywhere on the row ticks it, not only on the box. A click on the box itself is
    // already toggled by the list when itemClicked arrives, so compare with the state at press.
    auto pressed_state = std::make_shared<std::pair<QListWidgetItem *, Qt::CheckState>>(nullptr, Qt::Unchecked);
    connect(list, &QListWidget::itemPressed, &dialog, [pressed_state](QListWidgetItem *item) {
        *pressed_state = {item, item->checkState()};
    });
    connect(list, &QListWidget::itemClicked, &dialog, [pressed_state](QListWidgetItem *item) {
        if (!(item->flags() & Qt::ItemIsUserCheckable)) return;
        if (pressed_state->first == item && item->checkState() == pressed_state->second)
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        pressed_state->first = nullptr;
    });
    layout->addWidget(list);
    auto *manual = new QLineEdit;
    manual->setPlaceholderText("手动输入，多个用逗号分开，例如 YY.exe");
    layout->addWidget(manual);
    auto *buttons = new QHBoxLayout;
    auto *cancel = ui::Pill("取消", "sec");
    auto *ok = ui::Pill("添加", "acc");
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    for (int i = 0; i < list->count(); i++) {
        QListWidgetItem *item = list->item(i);
        if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked)
            AddProgramRow(item->data(Qt::UserRole).toString(), true);
    }
    for (QString exe : manual->text().split(QRegularExpression(R"([,，;；\s]+)"), Qt::SkipEmptyParts)) {
        if (!exe.endsWith(".exe", Qt::CaseInsensitive)) exe += ".exe";
        AddProgramRow(exe, true);
    }
    SaveConfig();
    UpdateChecks();
}

void LiteWindow::SetSourcesEditable(bool editable) {
    for (auto &r : rows_) {
        r->check->setEnabled(editable);
        if (r->device) r->device->setEnabled(editable);
        if (r->remove) r->remove->setVisible(editable);
    }
    add_program_->setVisible(editable);
    output_->setEnabled(editable);
    for (auto &e : models_) e.radio->setEnabled(editable && !e.path.isEmpty());
    choose_folder_->setEnabled(editable);
    lock_hint_->setText(editable ? "最近 30 秒，深色块是观众听到静音或替换音的地方" : "过滤中不能改来源和输出，要改先停止过滤");
    model_meta_->setText(editable ? "模型在本机识别说的话，不联网。" : "过滤中不能换模型，要换先停止过滤。模型在本机识别，不联网。");
}

// ============================================================================= start/stop

void LiteWindow::ToggleFilter() {
    if (pipeline_.running()) StopFilter();
    else StartFilter();
}

void LiteWindow::StartFilter() {
    start_error_.clear();
    UpdateChecks();
    model_path_ = paths_.model_dir.isEmpty() ? model_path_ : paths_.model_dir;
    if (ModelPath().isEmpty()) {
        start_error_ = "找不到识别模型，无法过滤";
        UpdateStatus();
        return;
    }
    if (output_->count() == 0) {
        start_error_ = "没有找到 VB-CABLE 的 CABLE Input，无法输出";
        UpdateStatus();
        return;
    }
    if (!EnableCable()) {
        start_error_ = "虚拟声卡“" + ShortOutputName() + "”被禁用了，自动启用没有成功";
        UpdateStatus();
        return;
    }
    UpdateChecks();
    if (check_problems_ > 0) {
        auto answer = QMessageBox::warning(this, "开播前检查有问题",
                                           "开播前检查里有标红的问题，可能会让未过滤的声音进入直播。\n仍然开始过滤吗？",
                                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }
    PipelineConfig c;
    Row *m = rows_[0].get();
    c.use_mic = m->check->isChecked() && m->device->count() > 0;
    c.mic_id = Wide(m->device->currentData().toString().toStdString());
    c.mic_name = m->device->currentText().toStdString();
    for (auto &r : rows_)
        if (!r->is_mic && r->check->isChecked()) c.apps.push_back(r->exe.toStdString());
    c.out_id = Wide(output_->currentData().toString().toStdString());
    c.out_name = OutputName().toStdString();
    ApplySettings();
    std::string error;
    if (!pipeline_.Start(c, &settings_, error)) {
        start_error_ = Q(error);
        CLOG(censor::kLogError, "[Lite] start failed: %s", error.c_str());
        UpdateStatus();
        return;
    }
    // Map pipeline sources back to rows (microphone first, then checked programs in order).
    size_t i = 0;
    for (auto &r : rows_) {
        r->source = nullptr;
        r->levels.clear();
        r->last_sound = 0;
        r->silent = false;
        r->backlog = 0;
    }
    if (c.use_mic) m->source = pipeline_.sources()[i++].get();
    for (auto &r : rows_)
        if (!r->is_mic && r->check->isChecked()) r->source = pipeline_.sources()[i++].get();
    started_ms_ = QDateTime::currentMSecsSinceEpoch();
    CLOG(censor::kLogInfo, "[Lite] started: microphone %s, %zu program(s), output %s", c.use_mic ? c.mic_name.c_str() : "off",
         c.apps.size(), c.out_name.c_str());
    SetSourcesEditable(false);
    Tick();
    UpdateChecks();
}

void LiteWindow::StopFilter() {
    pipeline_.Stop();
    for (auto &r : rows_) {
        r->source = nullptr;
        r->silent = false;
        r->backlog = 0;
    }
    CLOG(censor::kLogInfo, "[Lite] stopped");
    DisableCableIfAuto();
    SetSourcesEditable(true);
    Tick();
    UpdateChecks();
}

// ============================================================================= live state

void LiteWindow::Tick() {
    if (pipeline_.running() && !pipeline_.Maintain()) {
        start_error_ = "输出中断：" + Q(pipeline_.failure());
        CLOG(censor::kLogError, "[Lite] output failed: %s", pipeline_.failure().c_str());
        StopFilter();
        return;
    }
    const bool running = pipeline_.running();
    const double now_s = running ? (QDateTime::currentMSecsSinceEpoch() - started_ms_) / 1000.0 : 0;
    const double delay = settings_.Get().delay_seconds;
    const bool draw = isVisible() && pages_->currentIndex() == kFilter;
    for (auto &r : rows_) {
        Source *s = r->source;
        QString text, tone;
        r->bad = false;
        r->silent = false;
        if (!s) {
            r->level = 0;
            r->levels.clear();
            r->backlog = 0;
            const bool on = r->check->isChecked();
            text = !on ? "不过滤" : running ? "不过滤" : "未开始";
            r->text = text;
            if (draw) r->track->SetIdle(!on ? "没有选" : running ? "没有选" : "未开始");
        } else {
            const double db = Db(s->input_peak.exchange(0));
            r->level = Level01(db);
            if (db > kSilentDb) r->last_sound = now_s;
            r->levels.push_back(r->level);
            while (r->levels.size() > (size_t)ui::TrackView::kColumns) r->levels.pop_front();
            CensorEngine &e = s->engine;
            QString model_error;
            bool loading;
            {
                std::lock_guard<std::mutex> lock(e.history_mutex);
                loading = e.is_loading || e.loaded_model_path.empty();
                if (!e.initialization_error.empty()) model_error = Q(e.initialization_error);
            }
            {
                std::lock_guard<std::mutex> lock(e.queue_mutex);
                r->backlog = e.asr_queue.size() / 16000.0;
            }
            const size_t count = e.censored_count.load();
            const bool attached = s->kind() != Source::Kind::Process || s->attached();
            if (!attached) {
                text = s->pid() ? "正在接入…" : "没有运行，启动后自动接上";
                r->text = "没有运行";
            } else if (!model_error.isEmpty()) {
                text = "识别不可用：" + model_error + "（声音照常延迟输出，不会消音）";
                r->text = "识别不可用";
                r->bad = true;
            } else if (loading) {
                text = "正在加载识别模型";
                r->text = "加载中";
            } else if (r->backlog > delay - 0.5) {
                text = QString("识别跟不上，积压 %1 秒").arg(r->backlog, 0, 'f', 1);
                r->text = "识别跟不上";
                r->bad = true;
            } else if (r->is_mic && now_s >= kSilentSeconds && now_s - r->last_sound >= kSilentSeconds) {
                text = QString("%1 秒没有声音").arg((int)kSilentSeconds);
                r->text = "没有声音";
                r->bad = r->silent = true;
            } else {
                text = QString("过滤中，已消音 %1 次").arg(count);
                r->text = QString("已消音 %1 次").arg(count);
                tone = "ok";
            }
            if (r->bad) tone = "bad";
            if (draw) {
                if (!attached) r->track->SetIdle("没有运行");
                else {
                    std::vector<std::pair<double, double>> mutes;
                    const double rate = std::max<uint32_t>(1, e.sample_rate.load());
                    {
                        std::lock_guard<std::mutex> lock(e.beep_mutex);
                        const uint64_t tw = e.total_samples_written.load();
                        for (auto &[a, b] : e.mute_history)
                            mutes.emplace_back(((double)tw - (double)a) / rate, ((double)tw - (double)b) / rate);
                    }
                    r->track->SetData(std::vector<float>(r->levels.begin(), r->levels.end()), mutes);
                }
            }
        }
        if (r->state->text() != text) r->state->setText(text);
        SetProp(r->state, "tone", tone);
    }
    if (++tick_ % 2 == 0 || !running) UpdateStatus();
}

LiteWindow::Status LiteWindow::CurrentStatus() const {
    Status st;
    const bool running = pipeline_.running();
    bool loading = false, source_bad = false;
    size_t censored = 0;
    for (auto &r : rows_) {
        if (!r->source) continue;
        loading |= r->text == "加载中";
        source_bad |= r->bad;
        censored += r->source->engine.censored_count.load();
    }
    const int problems = check_problems_;
    const bool no_model = ModelPath().isEmpty();
    if (!running) {
        st.dot = Tone::Off;
        if (!start_error_.isEmpty()) {
            st.title = "没有开始过滤";
            st.subtitle = start_error_;
        } else if (no_model) {
            st.title = "没法开始过滤";
            st.subtitle = "找不到识别模型，选好模型后才能过滤。";
        } else {
            st.title = "未在过滤";
            st.subtitle = "现在推流软件收不到你的麦克风。开播前点“开始过滤”。";
        }
    } else {
        st.subtitle = QString("声音晚 %1 秒送进推流软件，已消音 %2 次").arg(settings_.Get().delay_seconds, 0, 'f', 1).arg(censored);
        if (loading) {
            st.title = "正在加载识别模型";
            st.subtitle = "几秒钟。加载完之前声音照常延迟输出，但还不会消音。";
            st.dot = Tone::Wait;
        } else if (problems || source_bad) {
            st.title = "正在过滤，有问题要处理";
            st.dot = Tone::Bad;
        } else {
            st.title = "正在过滤";
            st.dot = Tone::Ok;
        }
    }
    if (running && loading) st.chip_tone = Tone::Wait, st.chip = "加载中";
    else if (problems) st.chip_tone = Tone::Bad, st.chip = QString("%1 个问题").arg(problems);
    else if (running) st.chip_tone = Tone::Ok, st.chip = "检查通过";
    else st.chip_tone = Tone::Off, st.chip = "未开始";

    // One note for the tray window: the first problem, else the state.
    const ui::CheckItem *first_bad = nullptr;
    for (auto &c : checks_)
        if (c.tone == Tone::Bad) {
            first_bad = &c;
            break;
        }
    if (no_model && !running) {
        st.note_tone = Tone::Bad, st.note = "找不到识别模型", st.note_action = "去选择", st.note_page = kModel;
    } else if (!start_error_.isEmpty() && !running) {
        st.note_tone = Tone::Bad, st.note = start_error_;
    } else if (first_bad) {
        st.note_tone = Tone::Bad, st.note = first_bad->title, st.note_action = "查看", st.note_page = kCheck;
    } else if (running && loading) {
        st.note_tone = Tone::Wait, st.note = "正在加载识别模型，还不会消音";
    } else if (running) {
        st.note_tone = Tone::Ok, st.note = "开播前检查全部通过";
    } else {
        st.note_tone = Tone::Off, st.note = "开播前检查全部通过，可以开始过滤";
    }
    return st;
}

void LiteWindow::UpdateStatus() {
    const Status st = CurrentStatus();
    const bool running = pipeline_.running();
    const bool can_start = running || !ModelPath().isEmpty();
    if (title_->text() != st.title) title_->setText(st.title);
    if (subtitle_->text() != st.subtitle) subtitle_->setText(st.subtitle);
    ui::SetChip(chip_, st.chip_tone, st.chip);
    SetProp(go_, "kind", running ? "sec" : "acc");
    go_->setText(running ? "停止过滤" : "开始过滤");
    go_->setEnabled(can_start);

    // Banner
    QString banner, tone;
    if (!running && !start_error_.isEmpty())
        banner = "<b>没有开始过滤：</b>" + start_error_.toHtmlEscaped(), tone = "bad";
    else if (!running && ModelPath().isEmpty())
        banner = QString("<b>找不到识别模型。</b> <a href='%1' style='color:#9c2113'>去识别模型里选择</a>").arg(kModel), tone = "bad";
    else if (running && st.chip_tone == Tone::Wait)
        banner = "正在加载识别模型，加载完之前不会消音。", tone = "wait";
    if (banner_->text() != banner) banner_->setText(banner);
    SetProp(banner_, "tone", tone);
    banner_->setVisible(!banner.isEmpty());

    check_badge_->setText(QString::number(check_problems_));
    check_badge_->setVisible(check_problems_ > 0);

    if (popup_) {
        popup_->SetState(st.chip_tone, st.chip, st.title, st.subtitle, running, can_start);
        std::vector<TrayPopup::SourceLine> lines;
        for (auto &r : rows_) {
            if (!r->check->isChecked()) continue;
            lines.push_back({r->is_mic ? "麦克风" : r->exe, running ? r->text : "未开始", r->bad, r->level});
        }
        popup_->SetSources(lines);
        popup_->SetNote(st.note_tone, st.note, st.note_action);
    }
    if (tray_) {
        const bool dark = ui::TaskbarIsDark(); // the user can switch the taskbar colour any time
        if (st.dot != tray_dot_ || dark != tray_dark_) {
            tray_dot_ = st.dot;
            tray_dark_ = dark;
            tray_->setIcon(ui::TrayIcon(st.dot));
            setWindowIcon(ui::TrayIcon(st.dot));
        }
        tray_->setToolTip("直播消音：" + st.title);
        if (auto *toggle = tray_->contextMenu()->findChild<QAction *>("toggle")) {
            toggle->setText(running ? "停止过滤" : "开始过滤");
            toggle->setEnabled(can_start);
        }
    }
}

// ============================================================================= checks

void LiteWindow::UpdateChecks() {
    std::vector<ui::CheckItem> items;
    auto add = [&](Tone tone, const char *group, const QString &title, const QString &detail = {},
                   const QString &action = {}, std::function<void()> act = {}, bool primary = false) {
        ui::CheckItem c;
        c.tone = tone;
        c.group = group;
        c.title = title;
        c.detail = detail;
        c.action = action;
        c.act = std::move(act);
        c.action_primary = primary;
        items.push_back(std::move(c));
    };
    const char *gSrc = "声音来源", *gCable = "VB-CABLE 和 Windows", *gMate = "推流软件", *gAsr = "识别";
    const bool running = pipeline_.running();

    // Sources
    Row *m = rows_[0].get();
    bool any = m->check->isChecked();
    QString default_mic;
    for (int i = 0; i < m->device->count(); i++)
        if (m->device->itemText(i).endsWith("（默认）")) default_mic = m->device->itemText(i).chopped(4);
    if (m->check->isChecked()) {
        QString mic = m->device->currentText();
        const bool is_default = mic.endsWith("（默认）");
        if (is_default) mic.chop(4);
        if (m->device->count() == 0)
            add(Tone::Bad, gSrc, "没有找到麦克风", "在 Windows 声音设置里看麦克风有没有插好、有没有被禁用。");
        else if (m->silent && !is_default && !default_mic.isEmpty())
            add(Tone::Bad, gSrc, "选中的麦克风 30 秒没有声音",
                "选的是“" + mic.toHtmlEscaped() + "”，不是 Windows 默认麦克风“" + default_mic.toHtmlEscaped() + "”，可能选错了。",
                "改用默认麦克风",
                [this] {
                    Row *mr = rows_[0].get();
                    StopFilter();
                    for (int i = 0; i < mr->device->count(); i++)
                        if (mr->device->itemText(i).endsWith("（默认）")) mr->device->setCurrentIndex(i);
                    SaveConfig();
                    StartFilter();
                },
                true);
        else if (m->silent)
            add(Tone::Bad, gSrc, "麦克风 30 秒没有声音", "看麦克风有没有插好、有没有被静音（耳机线上的开关、Windows 声音设置）。");
        else if (running && m->source)
            add(Tone::Ok, gSrc, m->last_sound > 0 ? "麦克风有声音" : "麦克风已接上", mic.toHtmlEscaped());
        else
            add(Tone::Ok, gSrc, "已选麦克风", mic.toHtmlEscaped());
    }
    for (auto &r : rows_) {
        if (r->is_mic || !r->check->isChecked()) continue;
        any = true;
        if (!FindProcessRoot(r->exe.toStdString()))
            add(Tone::Note, gSrc, r->exe + " 没有运行", "启动后自动接上，不用重新开始");
    }
    if (!any) add(Tone::Bad, gSrc, "没有选择要过滤的麦克风或程序", "在“过滤”页勾选麦克风或添加程序。", "去选择", [this] { ShowPage(kFilter); });

    // VB-CABLE and Windows
    CableRenderState cable = GetCableRenderState();
    const QString out_id = output_->currentData().toString();
    bool out_active = false;
    for (auto &e : ListEndpoints(0)) out_active |= Q(Utf8(e.id)) == out_id;
    if (cable == CableRenderState::NotInstalled || output_->count() == 0)
        add(Tone::Bad, gCable, "没有安装 VB-CABLE 虚拟声卡",
            cable_setup_->busy() ? cable_setup_->text()
                                 : QString("消音后的声音要经过它送进推流软件。VB-CABLE 是 VB-Audio 的捐赠软件，"
                                           "可以在这里安装，也可以去<a href='%1'>官网</a>下载。").arg(kVbCableUrl),
            cable_setup_->busy() ? QString() : QString(cable_setup_->has_bundled() ? "安装" : "下载并安装"),
            [this] { InstallCable(); }, true);
    else if (!out_active && !running && cable_auto_->isChecked() && cable != CableRenderState::Enabled)
        add(Tone::Note, gCable, "CABLE Input 现在是禁用的", "开始过滤时自动启用，停止后自动禁用（“消音设置”里可以关掉）");
    else if (!out_active)
        add(Tone::Bad, gCable, "虚拟声卡“" + ShortOutputName() + "”被禁用或不可用", "可以直接启用；也可以在 Windows“声音 → 播放”里启用。",
            "启用", [this] { EnableCable(); UpdateChecks(); }, true);
    else {
        auto others = ProgramsPlayingTo(Wide(out_id.toStdString()));
        if (cable_restart_)
            add(Tone::Note, gCable, "刚装好 VB-CABLE，请重启电脑后再开播", "VB-Audio 要求装完重启，否则声音可能断续");
        else if (others.empty())
            add(Tone::Ok, gCable, "VB-CABLE 已安装并启用", "CABLE Input 只有本程序在放声音");
        else {
            QStringList names;
            for (auto &o : others) names << Q(o);
            add(Tone::Bad, gCable, "别的程序也在往 CABLE Input 放声音：" + names.join("、"),
                "这些声音会不经过滤进入直播。例如输出设成 CABLE 的 KOOK，或开着直播伴侣模式的 OBS 插件。");
        }
    }
    CompanionAudioState q = QueryCompanionAudio();
    auto restore = [this] {
        std::string message;
        const bool ok = RestoreDefaultPlayback(message);
        CLOG(ok ? censor::kLogInfo : censor::kLogWarning, "[Lite] restore default playback: %s", message.c_str());
        if (!ok) QMessageBox::warning(this, "没有改回", Q(message));
        UpdateChecks();
    };
    if (q.console_is_cable)
        add(Tone::Bad, gCable, "Windows 默认播放设备是 CABLE",
            "所有程序的声音都会不经过滤进入直播。可以直接改回耳机或音箱，也可以在 Windows 声音设置里改。", "改回", restore, true);
    if (q.communications_is_cable)
        add(Tone::Bad, gCable, "Windows 默认通信设备是 CABLE",
            "语音软件的声音会不经过滤进入直播（装 VB-CABLE 时 Windows 常会这样改）。可以直接改回耳机或音箱，"
            "也可以在“声音 → 播放”里改。", "改回", restore, true);
    if (!q.console_is_cable && !q.communications_is_cable)
        add(Tone::Ok, gCable, "Windows 默认播放和通信设备都不是 CABLE", "否则别的程序的声音会不经过滤进入直播");

    // Streaming program (直播伴侣, 直播姬, 虎牙, 斗鱼): streaming-apps.cpp
    for (const StreamingCheck &c : StreamingAppChecks(CableRecordingName(), paths_.companion_store))
        add(c.tone, gMate, c.title, c.detail);

    // Recognition
    QString model_name;
    for (auto &e : models_)
        if (!e.path.isEmpty() && QFileInfo(e.path) == QFileInfo(ModelPath())) model_name = e.name;
    if (ModelPath().isEmpty())
        add(Tone::Bad, gAsr, "找不到识别模型", "到“识别模型”里选一个模型，或选择模型文件夹。", "去选择", [this] { ShowPage(kModel); }, true);
    else {
        bool lag = false;
        for (auto &r : rows_) {
            if (!r->source || r->text != "识别跟不上") continue;
            lag = true;
            add(Tone::Bad, gAsr, QString("%1 识别跟不上（积压 %2 秒）").arg(r->is_mic ? "麦克风" : r->exe).arg(r->backlog, 0, 'f', 1),
                "可能漏消音。电脑太忙时，在“识别模型”里减少识别路数，还不行就减到 1 路。", "去调整", [this] { ShowPage(kModel); });
        }
        if (!lag)
            add(Tone::Ok, gAsr, "识别模型已就绪",
                QString("%1，%2 路").arg(model_name.isEmpty() ? QFileInfo(ModelPath()).fileName() : model_name).arg(EffectiveLanes()));
    }

    int problems = 0;
    QString signature;
    for (auto &c : items) {
        problems += c.tone == Tone::Bad;
        signature += QString::number((int)c.tone) + c.group + c.title + c.detail + c.action + "\n";
    }
    check_problems_ = problems;
    checks_ = std::move(items);
    if (signature != checks_signature_) {
        checks_signature_ = signature;
        RenderChecks();
    }
    UpdateStatus();
}

void LiteWindow::RenderChecks() {
    // Checks page: problems first under 需要处理, then the other groups in order.
    ClearLayout(check_list_);
    int bad = 0, ok = 0, notes = 0;
    for (auto &c : checks_) (c.tone == Tone::Bad ? bad : c.tone == Tone::Ok ? ok : notes)++;
    if (bad) {
        check_list_->addWidget(ui::MakeGroupLabel("需要处理"));
        for (auto &c : checks_)
            if (c.tone == Tone::Bad) check_list_->addWidget(ui::MakeCheckRow(c));
    }
    QString group;
    for (auto &c : checks_) {
        if (c.tone == Tone::Bad) continue;
        if (c.group != group) {
            group = c.group;
            check_list_->addWidget(ui::MakeGroupLabel(group));
        }
        check_list_->addWidget(ui::MakeCheckRow(c));
    }

    // Summary on the 过滤 page: only the problems, or one line.
    ClearLayout(check_summary_);
    if (bad) {
        for (auto &c : checks_)
            if (c.tone == Tone::Bad) check_summary_->addWidget(ui::MakeCheckRow(c));
    } else {
        ui::CheckItem c;
        c.tone = Tone::Ok;
        c.title = notes ? QString("全部通过（%1 项），另有 %2 条提示").arg(ok).arg(notes) : QString("全部通过（%1 项）").arg(ok);
        check_summary_->addWidget(ui::MakeCheckRow(c));
    }
}

// ============================================================================= window

void LiteWindow::closeEvent(QCloseEvent *event) {
    if (!quitting_ && QSystemTrayIcon::isSystemTrayAvailable()) {
        hide();
        if (!tray_hint_shown_) {
            tray_->showMessage("直播消音", pipeline_.running() ? "仍在过滤。点托盘图标打开小窗；要退出，右键托盘图标选“退出”。"
                                                             : "程序在托盘里。要退出，右键托盘图标选“退出”。");
            tray_hint_shown_ = true;
        }
        event->ignore();
        return;
    }
    popup_->hide();
    if (download_) download_->abort();
    cable_setup_->Cancel();
    pipeline_.Stop();
    DisableCableIfAuto();
    SaveConfig();
    event->accept();
    QApplication::quit();
}

void LiteWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::WindowStateChange && max_button_) {
        max_button_->SetMaximized(isMaximized());
        max_button_->setToolTip(isMaximized() ? "还原" : "最大化");
        max_button_->setAccessibleName(max_button_->toolTip());
    }
    QMainWindow::changeEvent(event);
}

bool LiteWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result) {
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        // The whole window is client area. A maximized window hangs over the screen edge by the
        // resize border, so it is cut back to the work area.
        if (IsZoomed(msg->hwnd)) {
            auto *p = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &mi)) p->rgrc[0] = mi.rcWork;
        }
        *result = 0;
        return true;
    }
    if (msg->message == WM_NCHITTEST) {
        RECT r;
        GetWindowRect(msg->hwnd, &r);
        const int x = GET_X_LPARAM(msg->lParam), y = GET_Y_LPARAM(msg->lParam);
        const qreal dpr = devicePixelRatioF();
        auto hit = [result](LRESULT code) {
            *result = code;
            return true;
        };
        if (!IsZoomed(msg->hwnd)) {
            const int b = (int)std::lround(6 * dpr);
            const bool left = x < r.left + b, right = x >= r.right - b, top = y < r.top + b, bottom = y >= r.bottom - b;
            if (top && left) return hit(HTTOPLEFT);
            if (top && right) return hit(HTTOPRIGHT);
            if (bottom && left) return hit(HTBOTTOMLEFT);
            if (bottom && right) return hit(HTBOTTOMRIGHT);
            if (left) return hit(HTLEFT);
            if (right) return hit(HTRIGHT);
            if (top) return hit(HTTOP);
            if (bottom) return hit(HTBOTTOM);
        }
        const QPoint local((int)((x - r.left) / dpr), (int)((y - r.top) / dpr));
        if (title_bar_ && local.y() < title_bar_->height() && !qobject_cast<QAbstractButton *>(childAt(local)))
            return hit(HTCAPTION);
        return hit(HTCLIENT);
    }
    return QMainWindow::nativeEvent(type, message, result);
}

// ============================================================================= VB-CABLE

void LiteWindow::InstallCable() {
    if (cable_setup_->busy()) return;
    const bool bundled = cable_setup_->has_bundled();
    QMessageBox box(QMessageBox::Question, bundled ? "安装 VB-CABLE" : "下载并安装 VB-CABLE",
                    bundled ? QString("将打开随本程序附带的 VB-CABLE 虚拟声卡原版安装程序（确认有 VB-Audio 的签名）：")
                            : QString("将从 VB-Audio 官网下载 VB-CABLE 虚拟声卡（约 1.3 MB），确认是 VB-Audio 签名的原版后，"
                                      "打开它自带的安装程序："),
                    QMessageBox::NoButton, this);
    box.setInformativeText("1. Windows 询问是否允许更改时，点“是”（安装驱动需要管理员权限）。\n"
                           "2. 在 VB-CABLE 的安装窗口里点“Install Driver”。\n"
                           "3. 装完重启电脑。\n\n"
                           "VB-CABLE 是 VB-Audio Software 的捐赠软件（www.vb-cable.com），不属于本程序。");
    QPushButton *go = box.addButton(bundled ? "安装" : "下载并安装", QMessageBox::AcceptRole);
    QPushButton *web = box.addButton("打开官网", QMessageBox::ActionRole);
    box.addButton("取消", QMessageBox::RejectRole);
    box.setDefaultButton(go);
    box.exec();
    if (box.clickedButton() == web) QDesktopServices::openUrl(QUrl(kVbCableUrl));
    if (box.clickedButton() != go) return;
    CLOG(censor::kLogInfo, "[Lite] VB-CABLE download and setup requested");
    cable_setup_->Start();
    UpdateChecks();
}

void LiteWindow::CableSetupFinished(bool ran, const QString &error) {
    if (quitting_) return;
    RefreshOutputs();
    if (output_->currentIndex() < 0 && output_->count() > 0) output_->setCurrentIndex(0);
    const bool installed = GetCableRenderState() != CableRenderState::NotInstalled;
    CLOG(censor::kLogInfo, "[Lite] VB-CABLE setup finished: %s", installed ? "installed" : "not installed");
    if (ran && installed) {
        cable_restart_ = true;
        QString text = "VB-CABLE 已经装好。请重启电脑后再开播（VB-Audio 要求装完重启）。";
        if (QueryCompanionAudio().communications_is_cable || QueryCompanionAudio().console_is_cable)
            text += "\n\n安装时 Windows 把默认播放或通信设备改成了 CABLE，在“开播前检查”里点“改回”。";
        QMessageBox::information(this, "VB-CABLE", text);
        ShowPage(kCheck);
    } else if (ran) {
        QMessageBox::information(this, "VB-CABLE",
                                 "VB-CABLE 安装程序已关闭，但还没检测到虚拟声卡。\n"
                                 "如果已经点了“Install Driver”，重启电脑后再看；也可以再点一次“下载并安装”。");
    } else if (!error.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, "VB-CABLE", error, QMessageBox::Ok, this);
        QPushButton *web = box.addButton("打开官网", QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == web) QDesktopServices::openUrl(QUrl(kVbCableUrl));
    }
    UpdateChecks();
}

bool LiteWindow::EnableCable() {
    const QString out_id = output_->currentData().toString();
    auto active = [&] {
        for (auto &e : ListEndpoints(0))
            if (Q(Utf8(e.id)) == out_id) return true;
        return false;
    };
    if (out_id.isEmpty()) return false;
    if (active()) return true;
    // Only the playback side (CABLE Input) is switched; it needs no administrator rights. The
    // user asked for it by starting the filter (or with 启用 in the checks).
    const bool ok = SetCableRenderEnabled(true);
    CLOG(ok ? censor::kLogInfo : censor::kLogWarning, "[Lite] VB-CABLE playback enabled%s", ok ? "" : " (failed)");
    bool on = false;
    for (int i = 0; ok && i < 30 && !(on = active()); i++) QThread::msleep(100); // Windows takes a moment
    RefreshOutputs();
    return on;
}

void LiteWindow::DisableCableIfAuto() {
    if (!cable_auto_ || !cable_auto_->isChecked()) return;
    const CableRenderState state = GetCableRenderState();
    if (state != CableRenderState::Enabled && state != CableRenderState::Mixed) return;
    const bool ok = SetCableRenderEnabled(false);
    CLOG(ok ? censor::kLogInfo : censor::kLogWarning, "[Lite] VB-CABLE playback disabled%s", ok ? "" : " (failed)");
    if (!quitting_) RefreshOutputs();
}

void LiteWindow::RefreshOutputs() {
    // Same items as FillDevices, re-labelled after the CABLE state changed; the selection stays.
    const QString id = output_->currentData().toString();
    const QSignalBlocker block(output_);
    output_->clear();
    for (auto &e : CableOutputs()) {
        AddOutputItem(output_, e, cable_auto_ && cable_auto_->isChecked());
    }
    const int i = output_->findData(id);
    if (i >= 0) output_->setCurrentIndex(i);
}
