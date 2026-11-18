#pragma once

// Main window of LiveSpeechFilter (approved prototype work/lite-ui-20261113/design-demos/dir-4.html):
// own title bar, sidebar pages 过滤 / 屏蔽词 / 消音设置 / 识别模型 / 开播前检查 / 支持项目, a tray
// icon whose dot shows the state, and a small tray window for daily use. Closing the window keeps the
// program in the tray; "退出（停止输出）" in the tray menu stops the output.

#include "pipeline.hpp"
#include "ui-kit.hpp"

#include <QByteArray>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QSystemTrayIcon>

#include <deque>
#include <memory>
#include <vector>

class CableSetup;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QFile;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class TrayPopup;

struct LitePaths {
    QString config_dir;       // %APPDATA%/LiveSpeechFilter (or --config-dir)
    QString companion_store;  // 直播伴侣 audioStore.json (or --companion-config)
    QString exe_dir;
    QString model_dir;        // --model (testing): overrides the model chosen in the window
    QString log_path;         // this run's log file
};

class LiteWindow : public QMainWindow {
    Q_OBJECT

public:
    enum Page { kFilter, kWords, kSettings, kModel, kCheck, kSupport, kPageCount };

    explicit LiteWindow(const LitePaths &paths, QWidget *parent = nullptr);
    ~LiteWindow() override;
    void ShowPage(Page page); // shows the window too

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    // Own title bar: the native one is removed (WM_NCCALCSIZE) while Windows keeps the resize
    // border, shadow, snapping and animations; WM_NCHITTEST makes the bar draggable.
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override;

private:
    struct Row {
        bool is_mic = false;
        QString exe;                 // program rows
        QWidget *frame = nullptr;
        QCheckBox *check = nullptr;
        QComboBox *device = nullptr; // microphone row
        QLabel *state = nullptr;
        QPushButton *remove = nullptr;
        ui::TrackView *track = nullptr;
        lsf::Source *source = nullptr; // while running
        std::deque<float> levels;      // one per 0.1 s, newest last
        float level = 0;               // newest, 0..1
        QString text;                  // short state for the tray window
        bool bad = false;
        double backlog = 0;            // seconds of audio waiting for recognition
        bool silent = false;           // microphone: no sound for 30 s while filtering
        double last_sound = 0;        // seconds since start when the level was last above silence
    };
    struct ModelEntry {
        QString id;              // catalog id (folder name), empty for other folders
        QString path;            // installed folder, empty when not installed
        QString name, size, note, url;
        bool bundled = false;    // in <exe_dir>/models (installed with the program)
        bool downloaded = false; // in <config_dir>/models (can be deleted)
        QRadioButton *radio = nullptr;
        QLabel *chip = nullptr;  // "正在使用"
    };
    struct Status {
        ui::Tone chip_tone, dot;
        QString chip, title, subtitle;
        ui::Tone note_tone;
        QString note, note_action;
        Page note_page = kCheck;
    };

    // Building
    QWidget *BuildSidebar();
    QWidget *BuildFilterPage();
    QWidget *BuildWordsPage();
    QWidget *BuildSettingsPage();
    QWidget *BuildModelPage();
    QWidget *BuildCheckPage();
    QWidget *BuildSupportPage();
    QWidget *BuildTitleBar();
    void BuildTray();

    // Settings
    void LoadConfig();
    void SaveConfig();
    void ApplySettings();
    void UpdateDelayUi();
    void FillDevices();
    void ScanModels();
    void ChooseModelFolder();
    void DownloadModel(const QString &id, const QString &url);
    void DownloadFinished();
    void InstallDownloaded(const QString &id, const QString &zip);
    void DeleteModel(const QString &id, const QString &path);
    QString DownloadDir() const { return paths_.config_dir + "/models"; }
    // VB-CABLE playback side (CABLE Input): enabled while filtering when cable_auto_ is on.
    bool EnableCable();
    void DisableCableIfAuto();
    void RefreshOutputs();
    void InstallCable(); // 下载并安装 in the checks
    void CableSetupFinished(bool ran, const QString &error);
    QString ModelPath() const;
    void PreviewEffect();
    bool AutostartEnabled() const;
    void SetAutostart(bool on);

    // Words
    QStringList ReadWords() const;
    bool WriteWords(const QStringList &words);
    void AddWord();
    void RebuildWordList();

    // Sources
    Row *AddProgramRow(const QString &exe, bool checked);
    void RemoveProgramRow(Row *row);
    void ShowAddProgramsDialog();
    void SetSourcesEditable(bool editable);
    void UpdateLanesHelp();
    int EffectiveLanes() const; // "自动" (0) resolved by core count
    QString OutputName() const;
    QString ShortOutputName() const; // "CABLE Input"

    // Running
    void StartFilter();
    void StopFilter();
    void ToggleFilter();
    void Tick();             // every 0.1 s
    void UpdateChecks();     // every 2 s
    Status CurrentStatus() const;
    void UpdateStatus();
    void RenderChecks();

    LitePaths paths_;
    mutable lsf::SharedSettings settings_; // Get() locks
    lsf::Pipeline pipeline_;
    std::vector<std::unique_ptr<Row>> rows_;
    std::vector<ModelEntry> models_;
    QString model_path_;     // chosen in the window (or --model)
    QString custom_model_;   // folder chosen with 选择文件夹
    QString start_error_;
    QNetworkAccessManager *net_ = nullptr;
    CableSetup *cable_setup_ = nullptr;
    bool cable_restart_ = false; // VB-CABLE installed in this run: restart Windows before going live
    QNetworkReply *download_ = nullptr;
    QFile *download_file_ = nullptr;
    QString download_id_;
    QString download_text_;  // "正在下载 42%" / "正在解压…"
    QLabel *download_label_ = nullptr;
    std::vector<ui::CheckItem> checks_;
    QString checks_signature_;
    int check_problems_ = 0;
    qint64 started_ms_ = 0;
    int tick_ = 0;
    bool loading_ = true;    // no saving while the window is built and the settings are read
    bool quitting_ = false;
    bool tray_hint_shown_ = false;
    QByteArray preview_wav_; // kept alive while PlaySound plays it
    std::vector<std::shared_ptr<const std::vector<float>>> clips_; // kSounds, null when the file is missing

    QStackedWidget *pages_ = nullptr;
    QButtonGroup *nav_ = nullptr;
    QLabel *words_count_ = nullptr;
    QLabel *check_badge_ = nullptr;

    // 过滤
    QLabel *title_ = nullptr, *subtitle_ = nullptr, *chip_ = nullptr;
    QPushButton *go_ = nullptr;
    QLabel *banner_ = nullptr;
    QLabel *lock_hint_ = nullptr;
    QVBoxLayout *source_rows_ = nullptr;
    QPushButton *add_program_ = nullptr;
    QComboBox *output_ = nullptr;
    QVBoxLayout *check_summary_ = nullptr;

    // 屏蔽词
    QLineEdit *word_edit_ = nullptr;
    ui::Segmented *word_kind_ = nullptr;
    QVBoxLayout *word_list_ = nullptr;
    QPushButton *words_edit_ = nullptr; // 编辑 / 完成
    bool editing_words_ = false;
    ui::Switch *use_pinyin_ = nullptr;

    // 消音设置
    ui::Segmented *effect_ = nullptr;
    QPushButton *preview_ = nullptr;
    QLabel *effect_help_ = nullptr;
    ui::Segmented *padding_ = nullptr;
    ui::Switch *delay_auto_ = nullptr;
    QDoubleSpinBox *delay_ = nullptr;
    QLabel *delay_help_ = nullptr;
    ui::Switch *autostart_ = nullptr;

    // 识别模型
    QLabel *model_meta_ = nullptr;
    QVBoxLayout *model_list_ = nullptr;
    QButtonGroup *model_group_ = nullptr;
    QPushButton *choose_folder_ = nullptr;
    ui::Segmented *lanes_ = nullptr;
    QLabel *lanes_help_ = nullptr;
    QSpinBox *offset_ = nullptr;

    // 开播前检查
    QVBoxLayout *check_list_ = nullptr;

    // 支持项目
    ui::Switch *debug_log_ = nullptr;
    ui::Switch *cable_auto_ = nullptr;
    QWidget *title_bar_ = nullptr;
    ui::WindowButton *max_button_ = nullptr;

    QTimer *tick_timer_ = nullptr;
    QTimer *check_timer_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    TrayPopup *popup_ = nullptr;
    ui::Tone tray_dot_ = ui::Tone::Note;
    bool tray_dark_ = false;
};
