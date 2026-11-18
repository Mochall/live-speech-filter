#pragma once

// Look of the Lite window: colours and sizes of the approved prototype
// (work/lite-ui-20261113/design-demos/dir-4.html, Spectrum 2 style grey scale), the style
// sheet, and the few widgets Qt has no style for (switch, segmented choice, status chip,
// 30-second source track, level meter, check row).

#include <QAbstractButton>
#include <QColor>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QVariant>
#include <QWidget>

#include <functional>
#include <utility>
#include <vector>

class QAbstractSpinBox;
class QButtonGroup;
class QHBoxLayout;
class QPushButton;

namespace ui {

namespace color {
inline const QColor g25{0xff, 0xff, 0xff}, g50{0xf8, 0xf8, 0xf8}, g75{0xf3, 0xf3, 0xf3}, g100{0xe9, 0xe9, 0xe9},
    g200{0xe1, 0xe1, 0xe1}, g300{0xda, 0xda, 0xda}, g400{0xc6, 0xc6, 0xc6}, g500{0x8f, 0x8f, 0x8f},
    g700{0x50, 0x50, 0x50}, g800{0x29, 0x29, 0x29}, g900{0x13, 0x13, 0x13};
inline const QColor blue{0x3b, 0x63, 0xfb}, red{0xd7, 0x32, 0x20}, red_dark{0x9c, 0x21, 0x13}, green{0x05, 0x83, 0x4e},
    green_dark{0x02, 0x5d, 0x3c}, orange{0xe8, 0x6a, 0x00};
} // namespace color

enum class Tone { Ok, Bad, Off, Wait, Note };

QString StyleSheet();
// Small rounded label with a dot: "检查通过", "2 个问题", "未开始", "加载中".
QLabel *MakeChip();
void SetChip(QLabel *chip, Tone tone, const QString &text);
// Program icon with a status dot (grey idle, orange loading, red problem, green ok).
QIcon TrayIcon(Tone dot);
// True when the taskbar is dark or shows a dark accent colour (Windows "show accent colour on
// Start and taskbar"); the small icon then uses white bars, the blue ones would blend in.
bool TaskbarIsDark();
// The program icon without a status dot (resources/icon, 16-256 px).
QIcon AppIcon();
enum class Glyph { Filter, Words, Settings, Model, Check, Heart };
QIcon NavIcon(Glyph glyph);
// Pill buttons: kind "acc" (blue), "sec" (outlined); size "s", "l" or default.
QPushButton *Pill(const QString &text, const char *kind, const char *size = nullptr);
QPushButton *Link(const QString &text);
// Number box with round "−" / "+" buttons beside it (the box's own arrows are hidden). The buttons
// step the box and emit its editingFinished; disabling the returned widget disables all three.
QWidget *Stepper(QAbstractSpinBox *box);
QLabel *Help(const QString &text);
// White rounded card with an optional title and small grey note.
QWidget *Card(const QString &title = {}, QLabel **note = nullptr, QHBoxLayout **title_row = nullptr);

// Minimize / maximize (restore when maximized) / close button of the window's own title bar.
class WindowButton : public QAbstractButton {
    Q_OBJECT
public:
    enum Kind { Minimize, Maximize, Close };
    explicit WindowButton(Kind kind, QWidget *parent = nullptr);
    void SetMaximized(bool maximized);
    QSize sizeHint() const override { return {46, 36}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    Kind kind_;
    bool maximized_ = false;
};

// Left-to-right layout that wraps to the next line (Qt's flow layout example).
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr, int hspacing = 8, int vspacing = 8);
    ~FlowLayout() override;
    void addItem(QLayoutItem *item) override;
    int count() const override { return (int)items_.size(); }
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return DoLayout(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override;

private:
    int DoLayout(const QRect &rect, bool test) const;
    std::vector<QLayoutItem *> items_;
    int hspacing_, vspacing_;
};

// A custom word as a rounded tag. In edit mode a small round x floats on its top-right corner;
// clicking it emits removeClicked.
class WordChip : public QWidget {
    Q_OBJECT
public:
    WordChip(const QString &text, bool allow, QWidget *parent = nullptr);
    void SetEditing(bool editing);
    QSize sizeHint() const override;

signals:
    void removeClicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF Pill() const;
    QRectF Badge() const;
    QString text_;
    bool allow_, editing_ = false, badge_hover_ = false;
};

class Switch : public QAbstractButton {
    Q_OBJECT
public:
    explicit Switch(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
};

// One of several options shown as pills; the chosen one has a dark inset outline.
class Segmented : public QWidget {
    Q_OBJECT
public:
    explicit Segmented(QWidget *parent = nullptr);
    void AddOption(const QString &text, const QVariant &data);
    int count() const;
    int current() const;
    QVariant currentData() const;
    int FindData(const QVariant &data) const;
    void SetCurrent(int index); // no signal

signals:
    void changed(int index);

private:
    QButtonGroup *group_;
    QHBoxLayout *layout_;
    std::vector<QVariant> data_;
};

// The last 30 seconds of one source: level bars (one per 0.1 s, newest right), dark blocks where
// the audience hears the replacement, a red line at "now". Idle: flat grey with a note.
class TrackView : public QWidget {
public:
    static constexpr int kColumns = 300;
    explicit TrackView(QWidget *parent = nullptr);
    void SetIdle(const QString &note);
    // levels: 0..1, oldest first, up to kColumns. mutes: (start, end) in seconds before now.
    void SetData(const std::vector<float> &levels, const std::vector<std::pair<double, double>> &mutes);
    QSize sizeHint() const override { return {400, 52}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool idle_ = true;
    QString note_;
    std::vector<float> levels_;
    std::vector<std::pair<double, double>> mutes_;
};

class Meter : public QWidget {
public:
    explicit Meter(QWidget *parent = nullptr);
    void SetLevel(float level); // 0..1

protected:
    void paintEvent(QPaintEvent *) override;

private:
    float level_ = 0;
};

// One line of the pre-stream check: round icon, title, grey detail, optional button.
struct CheckItem {
    Tone tone = Tone::Ok; // Ok, Bad or Note
    QString group;
    QString title;
    QString detail;
    QString action;              // button text, empty for none
    bool action_primary = false; // blue button
    std::function<void()> act;
};
QWidget *MakeCheckRow(const CheckItem &item, bool show_detail = true);
QWidget *MakeGroupLabel(const QString &text);

} // namespace ui
