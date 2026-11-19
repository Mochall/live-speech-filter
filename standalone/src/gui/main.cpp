// LiveSpeechFilter (直播消音) window program.
//
// Options (for testing; normal use needs none):
//   --config-dir <dir>        settings, word list and logs (default %APPDATA%/LiveSpeechFilter)
//   --companion-config <file> 直播伴侣 audioStore.json to check
//   --model <dir>             recognition model folder (default: the one chosen in the window)
//   --tray                    start in the tray without showing the window (used when starting with Windows)
//   --check-page              open on the 开播前检查 page (screenshots)

#include "lite-window.hpp"
#include "ui-kit.hpp"
#include "censor-log.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

#include <cstdio>
#include <mutex>

namespace {

std::mutex g_log_mutex;
FILE *g_log = nullptr;

void LogToFile(int level, const char *message) {
    const char *tag = level <= censor::kLogError ? "error" : level <= censor::kLogWarning ? "warning" : "info";
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log) return;
    QByteArray now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz").toUtf8();
    fprintf(g_log, "%s %s: %s\n", now.constData(), tag, message);
    fflush(g_log);
}

QString Arg(const QStringList &args, const QString &name) {
    int i = args.indexOf(name);
    return i >= 0 && i + 1 < args.size() ? args[i + 1] : QString();
}

} // namespace

static HANDLE g_running_mutex = nullptr;

void ReleaseRunningMutex() {
    if (g_running_mutex) CloseHandle(g_running_mutex);
    g_running_mutex = nullptr;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("LiveSpeechFilter");
    QApplication::setQuitOnLastWindowClosed(false); // the tray keeps running; LiteWindow quits
    app.setStyleSheet(ui::StyleSheet());

    // Lets the installer and uninstaller see that the program is running (Inno Setup AppMutex) and
    // ask to quit it first; never checked by the program itself. Kept open until exit.
    g_running_mutex = CreateMutexW(nullptr, FALSE, L"LiveSpeechFilter.Running");

    const QStringList args = QApplication::arguments();
    const QString appdata = qEnvironmentVariable("APPDATA");
    LitePaths paths;
    paths.exe_dir = QApplication::applicationDirPath();
    paths.config_dir = Arg(args, "--config-dir");
    if (paths.config_dir.isEmpty()) paths.config_dir = appdata + "/LiveSpeechFilter";
    paths.companion_store = Arg(args, "--companion-config");
    if (paths.companion_store.isEmpty()) paths.companion_store = appdata + "/webcast_mate/WBStore/audioStore.json";
    paths.model_dir = Arg(args, "--model");
    QDir().mkpath(paths.config_dir + "/logs");

    // One instance per config folder: two copies would both play into CABLE Input.
    QLockFile lock(paths.config_dir + "/instance.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        QMessageBox::information(nullptr, "直播消音", "程序已经在运行了，请在任务栏右下角的托盘图标里打开它。");
        return 0;
    }

    const QString log_path =
        paths.config_dir + "/logs/lite-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".log";
    paths.log_path = log_path;
    g_log = _wfopen((const wchar_t *)log_path.utf16(), L"w");
    censor::SetLogHandler(LogToFile);
    // Keep the 10 newest logs.
    QDir logs(paths.config_dir + "/logs");
    const QStringList old = logs.entryList({"lite-*.log"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = 10; i < old.size(); i++) logs.remove(old[i]);
    const bool unthrottled = lsf::DisablePowerThrottling();
    CLOG(unthrottled ? censor::kLogInfo : censor::kLogWarning, "[Lite] Windows power throttling %s",
         unthrottled ? "off" : "could not be turned off");

    LiteWindow window(paths);
    if (args.contains("--check-page")) window.ShowPage(LiteWindow::kCheck);
    else if (args.contains("--support-page")) window.ShowPage(LiteWindow::kSupport);
    else if (!args.contains("--tray")) window.show();
    const int code = app.exec();
    censor::SetLogHandler(nullptr);
    {
        std::lock_guard<std::mutex> l(g_log_mutex);
        if (g_log) fclose(g_log);
        g_log = nullptr;
    }
    return code;
}
