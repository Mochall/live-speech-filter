#pragma once

// Gets VB-CABLE for the user who clicked 安装 / 下载并安装 in the checks: uses the original Pack46 the
// installer put next to the program, otherwise finds the newest driver pack on
// vb-audio.com/Cable (falls back to Pack46), downloads it from download.vb-audio.com, checks it
// (Pack46: known SHA256; every pack: VBCABLE_Setup_x64.exe must carry a valid Authenticode
// signature of VB-Audio's author), unpacks it with tar.exe and starts VB-Audio's own setup program
// as administrator. The user clicks "Install Driver" there; Windows asks for a restart afterwards.
//
// The package is used as published (VB-Audio allows distribution AS IS together with an
// application as long as the user can tell it is VB-CABLE by VB-Audio, donationware:
// https://vb-audio.com/Services/licensing.htm). Nothing is installed silently.

#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTimer;

class CableSetup : public QObject {
    Q_OBJECT

public:
    // work_dir: a folder of our own (%APPDATA%/LiveSpeechFilter/vbcable), emptied before and after.
    // bundled_zip: the original VBCABLE_Driver_Pack46.zip installed next to the program; used instead
    // of a download when it exists.
    CableSetup(QNetworkAccessManager *net, const QString &work_dir, const QString &bundled_zip,
               QObject *parent = nullptr);
    ~CableSetup() override;

    void Start();
    void Cancel(); // aborts a download; a setup program already open is left to the user
    bool busy() const { return state_ != State::Idle; }
    bool has_bundled() const;
    QString text() const { return text_; } // "正在下载 VB-CABLE 42%" while busy

signals:
    void Changed(); // text() changed
    // ran: the setup program was started and has closed again (whatever the user chose there).
    // Otherwise error is a sentence for the user ("" when cancelled by the user).
    void Finished(bool ran, const QString &error);

private:
    enum class State { Idle, Page, Download, Unpack, Setup };
    void Download(int pack);
    void Unpack();
    void Launch();
    void Done(bool ran, const QString &error);
    void SetText(const QString &t);

    QNetworkAccessManager *net_;
    QString dir_;
    QString bundled_;
    State state_ = State::Idle;
    QString text_;
    int pack_ = 0;
    QNetworkReply *reply_ = nullptr;
    QFile *file_ = nullptr;
    QProcess *tar_ = nullptr;
    void *process_ = nullptr; // HANDLE of the running setup program
    QTimer *poll_ = nullptr;
};

// Signer of a file's valid Authenticode signature ("" when unsigned or invalid).
QString AuthenticodeSigner(const QString &path);
