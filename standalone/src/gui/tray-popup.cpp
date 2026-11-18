#include "tray-popup.hpp"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ui;

TrayPopup::TrayPopup(QWidget *parent) : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *frame = new QFrame;
    frame->setObjectName("popup");
    frame->setFixedWidth(360);
    outer->addWidget(frame);
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *top = new QHBoxLayout;
    top->setContentsMargins(16, 12, 12, 0);
    top->setSpacing(8);
    auto *name = new QLabel("直播消音");
    name->setStyleSheet("font-weight:800");
    top->addWidget(name);
    chip_ = MakeChip();
    top->addWidget(chip_);
    top->addStretch();
    auto *settings = new QToolButton;
    settings->setObjectName("iconbtn");
    settings->setIcon(NavIcon(Glyph::Settings));
    settings->setToolTip("消音设置");
    settings->setCursor(Qt::PointingHandCursor);
    connect(settings, &QToolButton::clicked, this, [this] {
        hide();
        emit openSettings();
    });
    top->addWidget(settings);
    v->addLayout(top);

    auto *big = new QVBoxLayout;
    big->setContentsMargins(16, 12, 16, 12);
    big->setSpacing(2);
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(12);
    title_ = new QLabel;
    title_->setProperty("role", "h4");
    title_->setWordWrap(true);
    titleRow->addWidget(title_, 1);
    go_ = new Switch("");
    go_->setToolTip("开始 / 停止过滤");
    // The switch only asks; its position always follows the real state (SetState), so a start
    // that is cancelled or fails springs back.
    connect(go_, &QAbstractButton::clicked, this, [this] {
        go_->setChecked(!go_->isChecked());
        emit startStop();
    });
    titleRow->addWidget(go_, 0, Qt::AlignVCenter);
    big->addLayout(titleRow);
    subtitle_ = new QLabel;
    subtitle_->setProperty("role", "meta");
    subtitle_->setWordWrap(true);
    big->addWidget(subtitle_);
    v->addLayout(big);

    auto *srcWrap = new QWidget;
    sources_ = new QVBoxLayout(srcWrap);
    sources_->setContentsMargins(16, 0, 16, 6);
    sources_->setSpacing(0);
    v->addWidget(srcWrap);

    auto *noteRow = new QWidget;
    auto *nh = new QHBoxLayout(noteRow);
    nh->setContentsMargins(16, 0, 16, 12);
    note_ = new QLabel;
    note_->setProperty("role", "note");
    note_->setWordWrap(true);
    note_->setTextFormat(Qt::PlainText);
    nh->addWidget(note_, 1);
    note_action_ = Link("");
    note_action_->setStyleSheet("font-size:12px");
    connect(note_action_, &QPushButton::clicked, this, [this] {
        hide();
        emit noteAction();
    });
    nh->addWidget(note_action_);
    v->addWidget(noteRow);

    auto *foot = new QWidget;
    foot->setObjectName("popfoot");
    foot->setAttribute(Qt::WA_StyledBackground);
    auto *fh = new QHBoxLayout(foot);
    fh->setContentsMargins(16, 12, 16, 12);
    auto *open = Pill("打开主窗口", "sec");
    connect(open, &QPushButton::clicked, this, [this] {
        hide();
        emit openMain();
    });
    fh->addWidget(open, 1);
    v->addWidget(foot);
}

void TrayPopup::SetState(Tone chip_tone, const QString &chip, const QString &title, const QString &subtitle, bool running,
                         bool can_start) {
    SetChip(chip_, chip_tone, chip);
    title_->setText(title);
    subtitle_->setText(subtitle);
    go_->setChecked(running);
    go_->setEnabled(running || can_start);
}

void TrayPopup::SetSources(const std::vector<SourceLine> &lines) {
    QStringList names;
    for (auto &l : lines) names << l.name;
    if (names != names_) {
        while (QLayoutItem *item = sources_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        lines_.clear();
        for (auto &l : lines) {
            auto *row = new QFrame;
            row->setProperty("row", "true");
            auto *g = new QVBoxLayout(row);
            g->setContentsMargins(0, 8, 0, 8);
            g->setSpacing(4);
            auto *h = new QHBoxLayout;
            auto *n = new QLabel(l.name);
            n->setProperty("role", "bold");
            n->setMinimumWidth(1);
            h->addWidget(n, 1);
            auto *t = new QLabel;
            t->setProperty("role", "state");
            h->addWidget(t);
            g->addLayout(h);
            auto *m = new Meter;
            g->addWidget(m);
            sources_->addWidget(row);
            lines_.push_back({n, t, m});
        }
        names_ = names;
        adjustSize();
    }
    for (size_t i = 0; i < lines.size(); i++) {
        auto &w = lines_[i];
        w.text->setText(lines[i].text);
        if (w.text->property("tone").toString() != (lines[i].bad ? "bad" : "")) {
            w.text->setProperty("tone", lines[i].bad ? "bad" : "");
            w.text->style()->unpolish(w.text);
            w.text->style()->polish(w.text);
        }
        w.meter->SetLevel(lines[i].level);
    }
}

void TrayPopup::SetNote(Tone tone, const QString &text, const QString &action) {
    const char *t = tone == Tone::Ok ? "ok" : tone == Tone::Bad ? "bad" : tone == Tone::Wait ? "wait" : "off";
    note_->setText(text);
    if (note_->property("tone").toString() != t) {
        note_->setProperty("tone", t);
        note_->style()->unpolish(note_);
        note_->style()->polish(note_);
    }
    note_action_->setText(action);
    note_action_->setVisible(!action.isEmpty());
}

void TrayPopup::ShowNear(const QRect &tray_rect) {
    adjustSize();
    QScreen *screen = tray_rect.isValid() ? QGuiApplication::screenAt(tray_rect.center()) : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect avail = screen->availableGeometry();
    QPoint pos(avail.right() - width() - 12, avail.bottom() - height() - 12);
    if (tray_rect.isValid()) {
        pos.setX(std::clamp(tray_rect.center().x() - width() / 2, avail.left() + 8, avail.right() - width() - 8));
        if (tray_rect.top() >= avail.bottom()) pos.setY(avail.bottom() - height() - 8); // taskbar at the bottom
        else if (tray_rect.bottom() <= avail.top()) pos.setY(avail.top() + 8);           // at the top
    }
    move(pos);
    show();
    raise();
    activateWindow();
}
