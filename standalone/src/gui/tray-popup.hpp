#pragma once

// Small window shown when the tray icon is clicked: state, a start/stop switch, each source with its
// level and count, one status note, and a button to the main window. Closes on a click outside.

#include "ui-kit.hpp"

#include <QFrame>

#include <vector>

class QPushButton;
class QVBoxLayout;

class TrayPopup : public QWidget {
    Q_OBJECT
public:
    struct SourceLine {
        QString name;
        QString text;
        bool bad = false;
        float level = 0; // 0..1
    };

    explicit TrayPopup(QWidget *parent = nullptr);
    void SetState(ui::Tone chip_tone, const QString &chip, const QString &title, const QString &subtitle, bool running,
                  bool can_start);
    void SetSources(const std::vector<SourceLine> &lines);
    void SetNote(ui::Tone tone, const QString &text, const QString &action);
    void ShowNear(const QRect &tray_rect);

signals:
    void startStop();
    void openMain();
    void openSettings();
    void noteAction();

private:
    struct LineWidgets {
        QLabel *name, *text;
        ui::Meter *meter;
    };
    QLabel *chip_, *title_, *subtitle_, *note_;
    ui::Switch *go_;
    QPushButton *note_action_;
    QVBoxLayout *sources_;
    std::vector<LineWidgets> lines_;
    QStringList names_;
};
