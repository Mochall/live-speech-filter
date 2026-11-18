#include "ui-kit.hpp"

#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {

QString StyleSheet() {
    return R"(
* { font-family: "Microsoft YaHei UI"; font-size: 14px; color: #131313; }
QMainWindow, QWidget#root, QWidget#side { background: #f3f3f3; }
QWidget#main { background: #ffffff; border-top: 1px solid #e1e1e1; border-left: 1px solid #e1e1e1; border-top-left-radius: 10px; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
QLabel#appname { font-size: 18px; font-weight: 800; padding: 6px 8px 10px 8px; }
QPushButton[nav="true"] { text-align: left; min-height: 32px; max-height: 32px; padding: 0 8px; border: 0; border-radius: 8px;
  background: transparent; color: #292929; }
QPushButton[nav="true"]:hover { background: #e9e9e9; }
QPushButton[nav="true"]:checked { background: #e1e1e1; font-weight: 700; }
QPushButton[nav="small"] { text-align: left; min-height: 28px; padding: 0 8px; border: 0; border-radius: 8px; background: transparent;
  color: #505050; font-size: 12px; }
QPushButton[nav="small"]:hover, QPushButton[nav="small"]:checked { background: #e1e1e1; }
QLabel[role="navcount"] { font-size: 12px; color: #505050; background: transparent; }
QLabel[role="badge"] { font-size: 12px; font-weight: 700; color: #ffffff; background: #d73220; border-radius: 9px; padding: 0 6px; min-height: 18px; max-height: 18px; }
QLabel[role="h1"] { font-size: 22px; font-weight: 800; }
QLabel[role="h4"] { font-size: 20px; font-weight: 800; }
QLabel[role="meta"], QLabel[role="help"] { font-size: 12px; color: #505050; }
QLabel[role="label"] { color: #505050; }
QLabel[role="bold"] { font-weight: 700; }
QLabel[role="state"] { font-size: 12px; color: #505050; }
QLabel[role="state"][tone="ok"] { color: #025d3c; }
QLabel[role="state"][tone="bad"] { color: #9c2113; }
QLabel[role="group"] { font-size: 12px; font-weight: 700; color: #505050; padding: 10px 16px 2px 16px; }
QFrame#card { background: #ffffff; border: 1px solid #e1e1e1; border-radius: 10px; }
QLabel#cardTitle { font-size: 16px; font-weight: 800; }
QFrame[row="true"] { border: 0; border-top: 1px solid #e9e9e9; background: transparent; }
QFrame[row="first"] { border: 0; background: transparent; }
QFrame[ck="bad"] { background: #fff6f5; }
QLabel[icon="ok"], QLabel[icon="bad"], QLabel[icon="note"] { min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; border-radius: 10px; font-size: 12px; font-weight: 700; qproperty-alignment: AlignCenter; }
QLabel[icon="ok"] { background: #d7f7e1; color: #025d3c; }
QLabel[icon="bad"] { background: #ffebe8; color: #9c2113; }
QLabel[icon="note"] { background: #e9e9e9; color: #505050; }
QLabel[role="chip"] { font-size: 12px; font-weight: 700; background: transparent; }
QLabel[role="banner"] { border-radius: 8px; padding: 10px 14px; }
QLabel[role="banner"][tone="bad"] { background: #ffebe8; color: #9c2113; }
QLabel[role="banner"][tone="wait"] { background: #fff6e7; color: #903300; }
QLabel[role="note"] { font-size: 12px; border-radius: 8px; padding: 10px 12px; }
QLabel[role="note"][tone="ok"] { background: #d7f7e1; color: #025d3c; }
QLabel[role="note"][tone="bad"] { background: #ffebe8; color: #9c2113; }
QLabel[role="note"][tone="off"] { background: #f3f3f3; color: #505050; }
QLabel[role="note"][tone="wait"] { background: #fff6e7; color: #903300; }
QLabel[role="tag"] { font-size: 12px; border: 1px solid transparent; border-radius: 10px; padding: 1px 8px; background: #e9e9e9; color: #292929; }
QLabel[role="tag"][tone="allow"] { background: #e5f0fe; color: #1d3ecf; }
QPushButton[kind] { font-weight: 700; border-radius: 16px; min-height: 28px; max-height: 28px; padding: 0 14px; }
QPushButton[kind="acc"] { background: #3b63fb; color: #ffffff; border: 2px solid #3b63fb; }
QPushButton[kind="acc"]:hover { background: #274dea; border-color: #274dea; }
QPushButton[kind="acc"]:disabled { background: #c6c6c6; border-color: #c6c6c6; color: #f8f8f8; }
QPushButton[kind="sec"] { background: transparent; color: #292929; border: 2px solid #c6c6c6; }
QPushButton[kind="sec"]:hover { border-color: #8f8f8f; }
QPushButton[kind="sec"]:disabled { color: #8f8f8f; border-color: #e1e1e1; }
QPushButton[sz="s"] { font-size: 12px; border-radius: 12px; min-height: 20px; max-height: 20px; padding: 0 10px; }
QPushButton[sz="l"] { font-size: 16px; border-radius: 20px; min-height: 36px; max-height: 36px; padding: 0 20px; }
QPushButton[sz="step"] { font-size: 16px; border-radius: 16px; min-width: 32px; max-width: 32px; min-height: 32px; max-height: 32px; padding: 0; }
QAbstractSpinBox[stepper="true"] { qproperty-alignment: AlignCenter; }
QPushButton[kind="link"] { border: 0; background: transparent; color: #274dea; font-weight: 700; padding: 0; text-align: left; }
QPushButton[kind="link"]:hover { color: #1d3ecf; }
QPushButton[kind="link"]:disabled { color: #8f8f8f; }
QPushButton[seg="true"] { border: 2px solid transparent; border-radius: 16px; min-height: 28px; max-height: 28px; padding: 0 12px;
  background: transparent; color: #292929; }
QPushButton[seg="true"]:hover { background: #e9e9e9; }
QPushButton[seg="true"]:checked { border-color: #292929; font-weight: 700; }
QPushButton[seg="true"]:disabled { color: #8f8f8f; }
QPushButton[seg="true"]:checked:disabled { border-color: #c6c6c6; }
QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox { border: 2px solid #dadada; border-radius: 8px; padding: 0 8px; min-height: 28px;
  background: #ffffff; selection-background-color: #3b63fb; }
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #3b63fb; }
QComboBox:disabled, QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background: #f3f3f3; color: #717171; }
QComboBox::drop-down { border: 0; width: 22px; }
QComboBox QAbstractItemView { border: 1px solid #c6c6c6; background: #ffffff; selection-background-color: #e9e9e9; selection-color: #131313; }
QComboBox[small="true"] { font-size: 12px; min-height: 20px; border-radius: 7px; padding: 0 6px; }
QCheckBox, QRadioButton { spacing: 6px; }
QListWidget { border: 1px solid #e1e1e1; border-radius: 8px; background: #ffffff; }
QListWidget::item { min-height: 34px; padding: 0 8px; border-top: 1px solid #e9e9e9; }
QListWidget::item:selected { background: #f3f3f3; color: #131313; }
QProgressBar { border: 0; background: #e9e9e9; border-radius: 2px; max-height: 4px; }
QProgressBar::chunk { background: #3b63fb; border-radius: 2px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #c6c6c6; border-radius: 3px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QFrame#popup { background: #ffffff; border: 1px solid #c6c6c6; border-radius: 10px; }
QWidget#popfoot { background: #f8f8f8; border-top: 1px solid #e9e9e9; border-bottom-left-radius: 10px; border-bottom-right-radius: 10px; }
QToolButton#iconbtn { border: 0; border-radius: 14px; min-width: 28px; min-height: 28px; background: transparent; }
QToolButton#iconbtn:hover { background: #e9e9e9; }
QDialog { background: #ffffff; }
QMenu { background: #ffffff; border: 1px solid #c6c6c6; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; }
QMenu::item:selected { background: #e9e9e9; color: #131313; }
QMenu::separator { height: 1px; background: #e9e9e9; margin: 4px 0; }
)";
}

static const char *ToneName(Tone t) {
    switch (t) {
    case Tone::Ok: return "ok";
    case Tone::Bad: return "bad";
    case Tone::Wait: return "wait";
    case Tone::Note: return "note";
    default: return "off";
    }
}

static void Repolish(QWidget *w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}

namespace {

// Fully rounded pill with a dot and the text, painted directly.
class StatusChip : public QLabel {
public:
    StatusChip() {
        setProperty("role", "chip");
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        return QSize(kPadX * 2 + kDot + kGap + fm.horizontalAdvance(text()), std::max(22, fm.height() + 6));
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QColor bg, fg;
        const QString tone = property("tone").toString();
        if (tone == "ok") bg = QColor(0xd7, 0xf7, 0xe1), fg = QColor(0x02, 0x5d, 0x3c);
        else if (tone == "bad") bg = QColor(0xff, 0xeb, 0xe8), fg = QColor(0x9c, 0x21, 0x13);
        else if (tone == "wait") bg = QColor(0xff, 0xec, 0xcf), fg = QColor(0x90, 0x33, 0x00);
        else bg = QColor(0xe9, 0xe9, 0xe9), fg = QColor(0x50, 0x50, 0x50);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        p.setBrush(fg);
        p.drawEllipse(QPointF(kPadX + kDot / 2.0, r.center().y()), kDot / 2.0, kDot / 2.0);
        p.setPen(fg);
        p.setFont(font());
        p.drawText(QRectF(kPadX + kDot + kGap, 0, width(), height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }

private:
    static constexpr int kPadX = 9, kDot = 6, kGap = 5;
};

} // namespace

QLabel *MakeChip() { return new StatusChip; }

void SetChip(QLabel *chip, Tone tone, const QString &text) {
    if (chip->text() == text && chip->property("tone").toString() == ToneName(tone)) return;
    chip->setText(text);
    chip->setProperty("tone", ToneName(tone));
    Repolish(chip); // font from the style sheet
    chip->updateGeometry();
    chip->update();
}

// Each size is drawn for its pixel grid (16-32 px: bars only; 48 px and up: blue plate), so
// Qt must not scale one into another.
static const int kIconSizes[] = {16, 20, 24, 32, 48, 64, 128, 256};

QIcon AppIcon() {
    QIcon icon;
    for (int size : kIconSizes) icon.addFile(QString(":/icon/%1.png").arg(size), QSize(size, size));
    return icon;
}

bool TaskbarIsDark() {
    const QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        QSettings::NativeFormat);
    if (reg.value("ColorPrevalence", 0).toInt() == 1) {
        // Accent colour as 0xAABBGGRR.
        const QSettings dwm("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM", QSettings::NativeFormat);
        const quint32 abgr = dwm.value("AccentColor", 0xffd47800u).toUInt();
        const double r = (abgr & 0xff) / 255.0, g = ((abgr >> 8) & 0xff) / 255.0, b = ((abgr >> 16) & 0xff) / 255.0;
        return 0.2126 * r + 0.7152 * g + 0.0722 * b < 0.6;
    }
    return reg.value("SystemUsesLightTheme", 1).toInt() == 0;
}

QIcon TrayIcon(Tone dot) {
    // Status dot in the bottom-right corner (the icon keeps x/y >= 60 % free for it): grey not
    // filtering, orange loading, green filtering, red problem. White ring keeps it apart from the bars.
    const QColor c = dot == Tone::Ok ? QColor(0x24, 0xb3, 0x6b) : dot == Tone::Bad ? color::red
                     : dot == Tone::Wait ? color::orange : color::g500;
    const bool white_bars = TaskbarIsDark();
    QIcon icon;
    for (int size : kIconSizes) {
        QPixmap pm(QString(":/icon/%1.png").arg(size));
        if (pm.isNull()) continue;
        QPainter p(&pm);
        if (white_bars && size <= 32) { // the bars-only version: recolour, keep the shape
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(pm.rect(), Qt::white);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        p.setRenderHint(QPainter::Antialiasing);
        const double r = std::max(2.5, size * 0.17), ring = std::max(1.0, size / 20.0);
        const QPointF centre(size - r - ring / 2, size - r - ring / 2);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(centre, r + ring / 2, r + ring / 2);
        p.setBrush(c);
        p.drawEllipse(centre, r - ring / 2, r - ring / 2);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QIcon NavIcon(Glyph glyph) {
    QIcon icon;
    for (int size : {18, 36}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 18.0, size / 18.0);
        p.setPen(QPen(color::g800, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        switch (glyph) {
        case Glyph::Filter:
            path.moveTo(2, 9);
            path.lineTo(4, 9);
            path.lineTo(6, 4);
            path.lineTo(9, 14);
            path.lineTo(11, 7);
            path.lineTo(12.5, 9);
            path.lineTo(16, 9);
            p.drawPath(path);
            break;
        case Glyph::Words:
            p.drawRoundedRect(QRectF(3, 3, 12, 12), 2, 2);
            p.drawLine(QPointF(6, 7), QPointF(12, 7));
            p.drawLine(QPointF(6, 10), QPointF(10, 10));
            break;
        case Glyph::Settings:
            p.drawLine(QPointF(3, 5), QPointF(11, 5));
            p.drawLine(QPointF(14, 5), QPointF(15, 5));
            p.drawLine(QPointF(3, 13), QPointF(4, 13));
            p.drawLine(QPointF(7, 13), QPointF(15, 13));
            p.drawEllipse(QPointF(12.5, 5), 1.6, 1.6);
            p.drawEllipse(QPointF(5.5, 13), 1.6, 1.6);
            break;
        case Glyph::Model:
            p.drawRoundedRect(QRectF(4, 4, 10, 10), 2, 2);
            for (double v : {7.0, 11.0}) {
                p.drawLine(QPointF(v, 1.5), QPointF(v, 4));
                p.drawLine(QPointF(v, 14), QPointF(v, 16.5));
                p.drawLine(QPointF(1.5, v), QPointF(4, v));
                p.drawLine(QPointF(14, v), QPointF(16.5, v));
            }
            break;
        case Glyph::Heart:
            path.moveTo(9, 15);
            path.cubicTo(3, 11, 1.8, 7.8, 2.6, 5.6);
            path.cubicTo(3.6, 2.8, 7.4, 2.4, 9, 5.4);
            path.cubicTo(10.6, 2.4, 14.4, 2.8, 15.4, 5.6);
            path.cubicTo(16.2, 7.8, 15, 11, 9, 15);
            p.drawPath(path);
            break;
        case Glyph::Check:
            p.drawEllipse(QPointF(9, 9), 6.5, 6.5);
            path.moveTo(6, 9.2);
            path.lineTo(8, 11.2);
            path.lineTo(12, 6.8);
            p.drawPath(path);
            break;
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QPushButton *Pill(const QString &text, const char *kind, const char *size) {
    auto *b = new QPushButton(text);
    b->setProperty("kind", kind);
    if (size) b->setProperty("sz", size); // not "size": that is QWidget's own QSize property
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QWidget *Stepper(QAbstractSpinBox *box) {
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setProperty("stepper", true);
    auto make = [box](const QString &text, int step) {
        auto *b = Pill(text, "sec", "step");
        b->setAutoRepeat(true);
        QObject::connect(b, &QPushButton::clicked, box, [box, step] {
            box->stepBy(step);
            emit box->editingFinished();
        });
        return b;
    };
    h->addWidget(make(QString(QChar(0x2212)), -1));
    h->addWidget(box);
    h->addWidget(make("+", 1));
    w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return w;
}

QPushButton *Link(const QString &text) {
    auto *b = new QPushButton(text);
    b->setProperty("kind", "link");
    b->setCursor(Qt::PointingHandCursor);
    b->setFlat(true);
    return b;
}

QLabel *Help(const QString &text) {
    auto *l = new QLabel(text);
    l->setProperty("role", "help");
    l->setWordWrap(true);
    return l;
}

QWidget *Card(const QString &title, QLabel **note, QHBoxLayout **title_row) {
    auto *card = new QFrame;
    card->setObjectName("card");
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    if (!title.isEmpty()) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(16, 14, 16, 4);
        row->setSpacing(8);
        auto *t = new QLabel(title);
        t->setObjectName("cardTitle");
        row->addWidget(t);
        auto *n = new QLabel;
        n->setProperty("role", "meta");
        row->addWidget(n);
        row->addStretch();
        v->addLayout(row);
        if (note) *note = n;
        if (title_row) *title_row = row;
    }
    return card;
}

// ----------------------------------------------------------------------------- Switch

WindowButton::WindowButton(Kind kind, QWidget *parent) : QAbstractButton(parent), kind_(kind) {
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(46, 36);
    setToolTip(kind == Minimize ? "最小化" : kind == Maximize ? "最大化" : "关闭");
}

void WindowButton::SetMaximized(bool maximized) {
    maximized_ = maximized;
    if (kind_ == Maximize) setToolTip(maximized ? "还原" : "最大化");
    update();
}

void WindowButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const bool hot = underMouse(), down = isDown();
    if (kind_ == Close && (hot || down)) p.fillRect(rect(), down ? QColor(0xc4, 0x2b, 0x1c).lighter(115) : QColor(0xc4, 0x2b, 0x1c));
    else if (hot || down) p.fillRect(rect(), down ? color::g300 : color::g200);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kind_ == Close && (hot || down) ? Qt::white : color::g800, 1.0));
    const QPointF c(width() / 2.0, height() / 2.0);
    const double s = 5;
    switch (kind_) {
    case Minimize:
        p.drawLine(QPointF(c.x() - s, c.y()), QPointF(c.x() + s, c.y()));
        break;
    case Maximize:
        if (maximized_) {
            p.drawRect(QRectF(c.x() - s, c.y() - s + 2, 2 * s - 2, 2 * s - 2));
            p.drawLine(QPointF(c.x() - s + 2, c.y() - s), QPointF(c.x() + s, c.y() - s));
            p.drawLine(QPointF(c.x() + s, c.y() - s), QPointF(c.x() + s, c.y() + s - 2));
        } else {
            p.drawRoundedRect(QRectF(c.x() - s, c.y() - s, 2 * s, 2 * s), 1, 1);
        }
        break;
    case Close:
        p.drawLine(QPointF(c.x() - s, c.y() - s), QPointF(c.x() + s, c.y() + s));
        p.drawLine(QPointF(c.x() - s, c.y() + s), QPointF(c.x() + s, c.y() - s));
        break;
    }
}

Switch::Switch(const QString &text, QWidget *parent) : QAbstractButton(parent) {
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
}

QSize Switch::sizeHint() const {
    return {32 + 10 + fontMetrics().horizontalAdvance(text()) + 2, std::max(22, fontMetrics().height() + 4)};
}

void Switch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.45);
    const double y = (height() - 18) / 2.0;
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? color::g800 : color::g400);
    p.drawRoundedRect(QRectF(0, y, 32, 18), 9, 9);
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(isChecked() ? 17 : 3, y + 3, 12, 12));
    p.setPen(color::g900);
    p.drawText(QRect(42, 0, width() - 42, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}

// ----------------------------------------------------------------------------- Segmented

Segmented::Segmented(QWidget *parent) : QWidget(parent), group_(new QButtonGroup(this)), layout_(new QHBoxLayout(this)) {
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(4);
    layout_->addStretch();
    group_->setExclusive(true);
    connect(group_, &QButtonGroup::idClicked, this, [this](int id) { emit changed(id); });
}

void Segmented::AddOption(const QString &text, const QVariant &data) {
    auto *b = new QPushButton(text);
    b->setProperty("seg", true);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    group_->addButton(b, (int)data_.size());
    layout_->insertWidget((int)data_.size(), b);
    data_.push_back(data);
}

int Segmented::count() const { return (int)data_.size(); }
int Segmented::current() const { return group_->checkedId(); }
QVariant Segmented::currentData() const {
    int i = current();
    return i >= 0 && i < (int)data_.size() ? data_[i] : QVariant();
}
int Segmented::FindData(const QVariant &data) const {
    for (size_t i = 0; i < data_.size(); i++)
        if (data_[i] == data) return (int)i;
    return -1;
}
void Segmented::SetCurrent(int index) {
    if (auto *b = group_->button(index)) b->setChecked(true);
}

// ----------------------------------------------------------------------------- TrackView

TrackView::TrackView(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(52);
    setMaximumHeight(52);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TrackView::SetIdle(const QString &note) {
    if (idle_ && note_ == note) return;
    idle_ = true;
    note_ = note;
    levels_.clear();
    mutes_.clear();
    update();
}

void TrackView::SetData(const std::vector<float> &levels, const std::vector<std::pair<double, double>> &mutes) {
    idle_ = false;
    levels_ = levels;
    mutes_ = mutes;
    update();
}

void TrackView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect();
    QPainterPath clip;
    clip.addRoundedRect(r, 7, 7);
    p.setClipPath(clip);
    p.fillRect(r, color::g50);
    const double W = r.width(), H = r.height(), w = W / kColumns;
    if (idle_) {
        p.setPen(Qt::NoPen);
        p.setBrush(color::g400);
        for (int i = 0; i < kColumns; i++) p.drawRect(QRectF(i * w + 0.5, H / 2 - 1, std::max(0.5, w - 1.5), 2));
        if (!note_.isEmpty()) {
            QFont f = font();
            f.setPixelSize(12);
            p.setFont(f);
            const QRect tr = p.fontMetrics().boundingRect(note_).adjusted(-8, -2, 8, 2);
            QRectF box(W / 2 - tr.width() / 2.0, H / 2 - tr.height() / 2.0, tr.width(), tr.height());
            p.setBrush(color::g50);
            p.drawRect(box);
            p.setPen(color::g700);
            p.drawText(box, Qt::AlignCenter, note_);
        }
        return;
    }
    auto x_of = [&](double ago) { return W * (1.0 - ago / 30.0); };
    p.setPen(Qt::NoPen);
    p.setBrush(color::g800);
    for (auto &[start, end] : mutes_) {
        const double x0 = std::max(0.0, x_of(start)), x1 = std::min(W, x_of(end));
        if (x1 > x0) p.drawRoundedRect(QRectF(x0, 8, std::max(3.0, x1 - x0), H - 16), 5, 5);
    }
    p.setBrush(color::g500);
    const int n = (int)levels_.size();
    for (int k = 0; k < n; k++) {
        const int col = kColumns - n + k;
        const double x = col * w;
        bool inside = false;
        for (auto &[start, end] : mutes_) inside |= x + w / 2 >= x_of(start) && x + w / 2 <= x_of(end);
        if (inside) continue;
        const double h = std::max(2.0, (double)levels_[k] * (H - 20));
        p.drawRect(QRectF(x + 0.5, H / 2 - h / 2, std::max(0.5, w - 1.5), h));
    }
    p.setBrush(color::red);
    p.drawRect(QRectF(W - 3, 0, 3, H));
}

// ----------------------------------------------------------------------------- Meter

Meter::Meter(QWidget *parent) : QWidget(parent) {
    setFixedHeight(4);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Meter::SetLevel(float level) {
    level = std::clamp(level, 0.0f, 1.0f);
    if (std::abs(level - level_) < 0.01f) return;
    level_ = level;
    update();
}

void Meter::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color::g100);
    p.drawRoundedRect(rect(), 2, 2);
    if (level_ > 0) {
        p.setBrush(color::g800);
        p.drawRoundedRect(QRectF(0, 0, width() * level_, height()), 2, 2);
    }
}

// ----------------------------------------------------------------------------- checks

QWidget *MakeCheckRow(const CheckItem &item, bool show_detail) {
    auto *row = new QFrame;
    row->setProperty("row", "true");
    if (item.tone == Tone::Bad) row->setProperty("ck", "bad");
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(16, 9, 16, 9);
    h->setSpacing(10);
    auto *icon = new QLabel(item.tone == Tone::Ok ? "✓" : item.tone == Tone::Bad ? "!" : "i");
    icon->setProperty("icon", item.tone == Tone::Ok ? "ok" : item.tone == Tone::Bad ? "bad" : "note");
    h->addWidget(icon, 0, Qt::AlignTop);
    auto *v = new QVBoxLayout;
    v->setSpacing(2);
    auto *title = new QLabel(item.title);
    title->setWordWrap(true);
    v->addWidget(title);
    if (show_detail && !item.detail.isEmpty()) {
        auto *d = new QLabel(item.detail);
        d->setProperty("role", "state");
        if (item.tone == Tone::Bad) d->setProperty("tone", "bad");
        d->setWordWrap(true);
        d->setTextFormat(Qt::RichText);
        d->setOpenExternalLinks(true);
        v->addWidget(d);
    }
    h->addLayout(v, 1);
    if (!item.action.isEmpty() && item.act) {
        auto *b = Pill(item.action, item.action_primary ? "acc" : "sec", "s");
        QObject::connect(b, &QPushButton::clicked, b, item.act);
        h->addWidget(b, 0, Qt::AlignTop);
    }
    return row;
}

QWidget *MakeGroupLabel(const QString &text) {
    auto *l = new QLabel(text);
    l->setProperty("role", "group");
    return l;
}

// ----------------------------------------------------------------------------- FlowLayout

FlowLayout::FlowLayout(QWidget *parent, int hspacing, int vspacing) : QLayout(parent), hspacing_(hspacing), vspacing_(vspacing) {
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout() {
    while (QLayoutItem *item = takeAt(0)) delete item;
}

void FlowLayout::addItem(QLayoutItem *item) { items_.push_back(item); }

QLayoutItem *FlowLayout::itemAt(int index) const {
    return index >= 0 && index < (int)items_.size() ? items_[index] : nullptr;
}

QLayoutItem *FlowLayout::takeAt(int index) {
    if (index < 0 || index >= (int)items_.size()) return nullptr;
    QLayoutItem *item = items_[index];
    items_.erase(items_.begin() + index);
    return item;
}

void FlowLayout::setGeometry(const QRect &rect) {
    QLayout::setGeometry(rect);
    DoLayout(rect, false);
}

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (QLayoutItem *item : items_) size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::DoLayout(const QRect &rect, bool test) const {
    const QMargins m = contentsMargins();
    const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = r.x(), y = r.y(), line = 0;
    for (QLayoutItem *item : items_) {
        if (item->widget() && item->widget()->isHidden()) continue;
        const QSize hint = item->sizeHint();
        if (x + hint.width() > r.right() + 1 && line > 0) {
            x = r.x();
            y += line + vspacing_;
            line = 0;
        }
        if (!test) item->setGeometry(QRect(QPoint(x, y), hint));
        x += hint.width() + hspacing_;
        line = std::max(line, hint.height());
    }
    return y + line - rect.y() + m.bottom();
}

// ----------------------------------------------------------------------------- WordChip

namespace {
constexpr int kBadge = 18;    // x circle
constexpr int kBadgeOut = 6;  // how far it sticks out of the tag
} // namespace

WordChip::WordChip(const QString &text, bool allow, QWidget *parent) : QWidget(parent), text_(text), allow_(allow) {
    setMouseTracking(true);
    setToolTip(allow ? "例外，不屏蔽" : "屏蔽");
}

void WordChip::SetEditing(bool editing) {
    editing_ = editing;
    badge_hover_ = false;
    setCursor(Qt::ArrowCursor);
    update();
}

QSize WordChip::sizeHint() const {
    QFont f = font();
    f.setPixelSize(14);
    return {QFontMetrics(f).horizontalAdvance(text_) + 28 + kBadgeOut, 32 + kBadgeOut};
}

QRectF WordChip::Pill() const { return QRectF(0.5, kBadgeOut + 0.5, width() - kBadgeOut - 1, height() - kBadgeOut - 1); }

QRectF WordChip::Badge() const { return QRectF(width() - kBadge, 0, kBadge, kBadge); }

void WordChip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF pill = Pill();
    p.setPen(QPen(allow_ ? QColor(0xc4, 0xd6, 0xfc) : color::g200, 1));
    p.setBrush(allow_ ? QColor(0xe5, 0xf0, 0xfe) : color::g75);
    p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
    QFont f = font();
    f.setPixelSize(14);
    p.setFont(f);
    p.setPen(allow_ ? QColor(0x1d, 0x3e, 0xcf) : color::g800);
    p.drawText(pill, Qt::AlignCenter, text_);
    if (!editing_) return;
    const QRectF b = Badge().adjusted(1, 1, -1, -1);
    p.setPen(QPen(Qt::white, 1.5));
    p.setBrush(badge_hover_ ? color::red : color::g700);
    p.drawEllipse(b);
    const QPointF c = b.center();
    const double d = 3.5;
    p.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c + QPointF(-d, -d), c + QPointF(d, d));
    p.drawLine(c + QPointF(-d, d), c + QPointF(d, -d));
}

void WordChip::mousePressEvent(QMouseEvent *event) {
    if (editing_ && event->button() == Qt::LeftButton && Badge().adjusted(-3, -3, 3, 3).contains(event->position()))
        emit removeClicked();
}

void WordChip::mouseMoveEvent(QMouseEvent *event) {
    const bool hover = editing_ && Badge().adjusted(-3, -3, 3, 3).contains(event->position());
    if (hover == badge_hover_) return;
    badge_hover_ = hover;
    setCursor(hover ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void WordChip::leaveEvent(QEvent *) {
    if (!badge_hover_) return;
    badge_hover_ = false;
    update();
}

} // namespace ui
