#include "cable-setup.hpp"

#include "censor-log.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include <windows.h>
#include <shellapi.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

namespace {

const char *kCablePage = "https://vb-audio.com/Cable/";
const char *kPackUrl = "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack%1.zip";
const int kKnownPack = 46;
const char *kKnownSha256 = "bc44e8974191a5a2427d6c979e2dab08dbb09925f3f435dfa2f0ba3e45b6eb90";
const qint64 kMaxBytes = 50 * 1024 * 1024; // Pack46 is 1.3 MB
const char *kSetupExe = "VBCABLE_Setup_x64.exe";

// VB-Audio signs with the certificate of its author ("BUREL VINCENT Entrepreneur individuel").
bool IsVbAudioSigner(const QString &signer) {
    return signer.contains("BUREL VINCENT", Qt::CaseInsensitive) || signer.contains("VB-Audio", Qt::CaseInsensitive);
}

} // namespace

QString AuthenticodeSigner(const QString &path) {
    const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
    WINTRUST_FILE_INFO info{sizeof(info)};
    info.pcwszFilePath = file.c_str();
    WINTRUST_DATA data{sizeof(data)};
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &info;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    QString signer;
    if (status == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA *prov = WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR *sgnr = prov ? WTHelperGetProvSignerFromChain(prov, 0, FALSE, 0) : nullptr;
        if (sgnr && sgnr->csCertChain > 0 && sgnr->pasCertChain[0].pCert) {
            wchar_t name[512] = {};
            CertGetNameStringW(sgnr->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512);
            signer = QString::fromWCharArray(name);
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return signer;
}

CableSetup::CableSetup(QNetworkAccessManager *net, const QString &work_dir, const QString &bundled_zip,
                       QObject *parent)
    : QObject(parent), net_(net), dir_(work_dir), bundled_(bundled_zip) {
    poll_ = new QTimer(this);
    poll_->setInterval(500);
    connect(poll_, &QTimer::timeout, this, [this] {
        if (!process_ || WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return;
        DWORD code = 0;
        GetExitCodeProcess(process_, &code);
        CloseHandle(process_);
        process_ = nullptr;
        poll_->stop();
        CLOG(censor::kLogInfo, "[Lite] VB-CABLE setup closed (exit code %lu)", code);
        QDir(dir_).removeRecursively();
        Done(true, {});
    });
}

CableSetup::~CableSetup() {
    if (process_) CloseHandle(process_);
}

bool CableSetup::has_bundled() const { return !bundled_.isEmpty() && QFile::exists(bundled_); }

void CableSetup::SetText(const QString &t) {
    text_ = t;
    emit Changed();
}

void CableSetup::Done(bool ran, const QString &error) {
    state_ = State::Idle;
    text_.clear();
    emit Finished(ran, error);
}

void CableSetup::Start() {
    if (busy()) return;
    QDir(dir_).removeRecursively();
    if (!QDir().mkpath(dir_)) {
        Done(false, "不能创建下载文件夹：" + QDir::toNativeSeparators(dir_));
        return;
    }
    if (has_bundled()) {
        // The installer's copy (checked against kKnownSha256 when the payload was built); the
        // signature check in Launch() still decides.
        pack_ = kKnownPack;
        const QString copy = dir_ + QString("/VBCABLE_Driver_Pack%1.zip").arg(pack_);
        if (QFile::copy(bundled_, copy)) {
            CLOG(censor::kLogInfo, "[Lite] VB-CABLE: using the pack installed with the program");
            Unpack();
            return;
        }
        CLOG(censor::kLogWarning, "[Lite] VB-CABLE: the pack installed with the program could not be copied");
    }
    // The newest pack is the highest VBCABLE_Driver_PackNN.zip on the download page.
    state_ = State::Page;
    SetText("正在查找最新的 VB-CABLE…");
    QNetworkRequest request{QUrl(kCablePage)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    reply_ = net_->get(request);
    connect(reply_, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = reply_;
        reply_ = nullptr;
        r->deleteLater();
        if (r->error() == QNetworkReply::OperationCanceledError) {
            Done(false, {});
            return;
        }
        int pack = 0;
        const QString html = QString::fromUtf8(r->readAll());
        auto it = QRegularExpression(R"(VBCABLE_Driver_Pack(\d{2,3})\.zip)").globalMatch(html);
        while (it.hasNext()) pack = (std::max)(pack, it.next().captured(1).toInt());
        CLOG(censor::kLogInfo, "[Lite] VB-CABLE page: %s, newest pack %d", r->error() ? "not read" : "read", pack);
        Download((std::max)(pack, kKnownPack));
    });
}

void CableSetup::Download(int pack) {
    pack_ = pack;
    state_ = State::Download;
    file_ = new QFile(dir_ + QString("/VBCABLE_Driver_Pack%1.zip").arg(pack), this);
    if (!file_->open(QIODevice::WriteOnly)) {
        delete file_;
        file_ = nullptr;
        Done(false, "不能写下载文件：" + QDir::toNativeSeparators(dir_));
        return;
    }
    SetText("正在下载 VB-CABLE…");
    const QString url = QString(kPackUrl).arg(pack);
    CLOG(censor::kLogInfo, "[Lite] VB-CABLE download started: %s", url.toUtf8().constData());
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    reply_ = net_->get(request);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        file_->write(reply_->readAll());
        if (file_->size() > kMaxBytes) reply_->abort();
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) SetText(QString("正在下载 VB-CABLE %1%").arg(got * 100 / total));
    });
    connect(reply_, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = reply_;
        reply_ = nullptr;
        r->deleteLater();
        file_->write(r->readAll());
        file_->close();
        const qint64 size = file_->size();
        const QString path = file_->fileName();
        file_->deleteLater();
        file_ = nullptr;
        if (size > kMaxBytes) {
            Done(false, "下载的文件大小不对，已放弃。");
            return;
        }
        if (r->error() == QNetworkReply::OperationCanceledError) {
            Done(false, {});
            return;
        }
        if (r->error() != QNetworkReply::NoError) {
            CLOG(censor::kLogWarning, "[Lite] VB-CABLE download failed: %s", r->errorString().toUtf8().constData());
            Done(false, "没有下载成功，可以稍后再试，或到官网自己下载。\n" + r->errorString());
            return;
        }
        if (pack_ == kKnownPack) {
            QFile f(path);
            f.open(QIODevice::ReadOnly);
            const QByteArray sha = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex();
            if (sha != kKnownSha256) {
                // VB-Audio may republish a pack under the same name: the signature check below decides.
                CLOG(censor::kLogWarning, "[Lite] VB-CABLE Pack46 SHA256 differs: %s", sha.constData());
            }
        }
        Unpack();
    });
}

void CableSetup::Unpack() {
    // Same as the model download: tar.exe (Windows 10 1803+) reads zip files; it runs in a folder
    // of its own with a relative name.
    state_ = State::Unpack;
    SetText("正在解压 VB-CABLE…");
    QDir(dir_).mkpath("x");
    tar_ = new QProcess(this);
    tar_->setWorkingDirectory(dir_ + "/x");
    auto done = [this](bool ok) {
        tar_->deleteLater();
        tar_ = nullptr;
        if (!ok) {
            Done(false, "VB-CABLE 包没能解压。可以稍后再试，或到官网自己下载。");
            return;
        }
        Launch();
    };
    connect(tar_, &QProcess::finished, this,
            [done](int code, QProcess::ExitStatus status) { done(status == QProcess::NormalExit && code == 0); });
    connect(tar_, &QProcess::errorOccurred, this, [done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) done(false);
    });
    tar_->start(qEnvironmentVariable("SystemRoot", "C:\\Windows") + "\\System32\\tar.exe",
                {"-xf", QString("../VBCABLE_Driver_Pack%1.zip").arg(pack_)});
}

void CableSetup::Launch() {
    const QString folder = dir_ + "/x";
    const QString exe = folder + "/" + kSetupExe;
    if (!QFile::exists(exe)) {
        Done(false, QString("VB-CABLE 包里没有 %1。可以到官网自己下载。").arg(kSetupExe));
        return;
    }
    const QString signer = AuthenticodeSigner(exe);
    CLOG(censor::kLogInfo, "[Lite] VB-CABLE Pack%d setup signed by: %s", pack_,
         signer.isEmpty() ? "(no valid signature)" : signer.toUtf8().constData());
    if (!IsVbAudioSigner(signer)) {
        QDir(dir_).removeRecursively();
        Done(false, "VB-CABLE 安装程序没有 VB-Audio 的有效数字签名，为安全起见没有打开。请到官网自己下载。");
        return;
    }
    // VB-Audio's setup needs administrator rights: Windows shows its UAC prompt.
    state_ = State::Setup;
    SetText("VB-CABLE 安装程序已打开，在里面点“Install Driver”");
    const std::wstring file = QDir::toNativeSeparators(exe).toStdWString();
    const std::wstring dir = QDir::toNativeSeparators(folder).toStdWString();
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = file.c_str();
    sei.lpDirectory = dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        const DWORD error = GetLastError();
        CLOG(censor::kLogWarning, "[Lite] VB-CABLE setup not started (error %lu)", error);
        QDir(dir_).removeRecursively();
        Done(false, error == ERROR_CANCELLED ? QString() : QString("没能打开 VB-CABLE 安装程序（错误 %1）。").arg(error));
        return;
    }
    CLOG(censor::kLogInfo, "[Lite] VB-CABLE setup started");
    process_ = sei.hProcess;
    poll_->start();
}

void CableSetup::Cancel() {
    if (reply_) reply_->abort();
    if (tar_) tar_->kill();
    if (process_) { // the setup window stays with the user; its files stay too
        CloseHandle(process_);
        process_ = nullptr;
        poll_->stop();
        state_ = State::Idle;
    }
}
