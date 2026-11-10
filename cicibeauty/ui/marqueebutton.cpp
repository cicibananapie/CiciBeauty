// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "marqueebutton.h"

#include <algorithm>

#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTimerEvent>

namespace {
constexpr int kMarqueeTimerIntervalMs = 16;
constexpr int kMinimumMarqueeSpeed = 1;
}

MarqueeButton::MarqueeButton(QWidget* parent)
    : QPushButton(parent) {
}

MarqueeButton::MarqueeButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent) {
}

MarqueeButton::~MarqueeButton() {
    stopMarqueeTimer();
}

void MarqueeButton::setMarqueeSpeed(int pxPerSecond) {
    const int speed = (std::max)(pxPerSecond, kMinimumMarqueeSpeed);
    if (marqueeSpeed_ == speed) {
        return;
    }

    marqueeSpeed_ = speed;
    if (hovering_) {
        updateMarqueeState();
        update();
    }
}

int MarqueeButton::marqueeSpeed() const {
    return marqueeSpeed_;
}

void MarqueeButton::setMarqueeGap(int px) {
    const int gap = (std::max)(px, 0);
    if (marqueeGap_ == gap) {
        return;
    }

    marqueeGap_ = gap;
    if (hovering_) {
        updateMarqueeState();
        update();
    }
}

int MarqueeButton::marqueeGap() const {
    return marqueeGap_;
}

void MarqueeButton::setMarqueeStartDelay(int ms) {
    const int delay = (std::max)(ms, 0);
    if (marqueeStartDelay_ == delay) {
        return;
    }

    marqueeStartDelay_ = delay;
    if (hovering_) {
        marqueeElapsed_.restart();
        updateMarqueeState();
        update();
    }
}

int MarqueeButton::marqueeStartDelay() const {
    return marqueeStartDelay_;
}

void MarqueeButton::paintEvent(QPaintEvent* event) {
    QStyleOptionButton opt;
    initStyleOption(&opt);

    QRect textRect;
    int width = 0;
    if (!textOverflows(opt, &textRect, &width)) {
        QPushButton::paintEvent(event);
        return;
    }

    QPainter painter(this);
    QStyleOptionButton buttonOpt(opt);
    buttonOpt.text.clear();
    style()->drawControl(QStyle::CE_PushButton, &buttonOpt, &painter, this);

    if (!hovering_ || !shouldMarquee(opt, &textRect, &width)) {
        drawElidedText(painter, opt, textRect);
        return;
    }

    updateMarqueeState();
    drawMarqueeText(painter, opt, textRect, width);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void MarqueeButton::enterEvent(QEnterEvent* event) {
#else
void MarqueeButton::enterEvent(QEvent* event) {
#endif
    QPushButton::enterEvent(event);
    hovering_ = true;
    marqueeElapsed_.restart();
    updateMarqueeState();
    update();
}

void MarqueeButton::leaveEvent(QEvent* event) {
    QPushButton::leaveEvent(event);
    hovering_ = false;
    stopMarqueeTimer();
    update();
}

void MarqueeButton::timerEvent(QTimerEvent* event) {
    if (event->timerId() != marqueeTimer_.timerId()) {
        QPushButton::timerEvent(event);
        return;
    }

    QStyleOptionButton opt;
    initStyleOption(&opt);
    if (!hovering_ || !shouldMarquee(opt)) {
        stopMarqueeTimer();
    }

    update();
}

void MarqueeButton::hideEvent(QHideEvent* event) {
    hovering_ = false;
    stopMarqueeTimer();
    QPushButton::hideEvent(event);
}

void MarqueeButton::resizeEvent(QResizeEvent* event) {
    QPushButton::resizeEvent(event);
    updateMarqueeState();
}

void MarqueeButton::changeEvent(QEvent* event) {
    QPushButton::changeEvent(event);

    switch (event->type()) {
    case QEvent::EnabledChange:
    case QEvent::FontChange:
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::LayoutDirectionChange:
        updateMarqueeState();
        update();
        break;
    default:
        break;
    }
}

QRect MarqueeButton::contentRect(const QStyleOptionButton& opt) const {
    QRect rect = style()->subElementRect(QStyle::SE_PushButtonContents, &opt, this);
    if (opt.state & (QStyle::State_Sunken | QStyle::State_On)) {
        rect.translate(style()->pixelMetric(QStyle::PM_ButtonShiftHorizontal, &opt, this),
                       style()->pixelMetric(QStyle::PM_ButtonShiftVertical, &opt, this));
    }

    return rect;
}

int MarqueeButton::textWidth(const QStyleOptionButton& opt) const {
    const QFontMetrics metrics(font());
    return metrics.size(textFlags(opt), opt.text).width();
}

int MarqueeButton::textFlags(const QStyleOptionButton& opt) const {
    int flags = Qt::TextSingleLine | Qt::AlignVCenter;
    if (style()->styleHint(QStyle::SH_UnderlineShortcut, &opt, this)) {
        flags |= Qt::TextShowMnemonic;
    } else {
        flags |= Qt::TextHideMnemonic;
    }

    return flags;
}

int MarqueeButton::marqueeOffset(int width) const {
    if (!marqueeElapsed_.isValid()) {
        return 0;
    }

    const qint64 elapsed = marqueeElapsed_.elapsed() - marqueeStartDelay_;
    if (elapsed <= 0) {
        return 0;
    }

    const int cycleWidth = width + marqueeGap_;
    if (cycleWidth <= 0) {
        return 0;
    }

    return static_cast<int>((elapsed * marqueeSpeed_ / 1000) % cycleWidth);
}

bool MarqueeButton::textOverflows(const QStyleOptionButton& opt, QRect* outContentRect, int* outTextWidth) const {
    if (opt.text.isEmpty() || !opt.icon.isNull()) {
        return false;
    }

    const QRect rect = contentRect(opt);
    const int width = textWidth(opt);
    const bool shouldScroll = rect.width() > 0 && width > rect.width();
    if (outContentRect != nullptr) {
        *outContentRect = rect;
    }
    if (outTextWidth != nullptr) {
        *outTextWidth = width;
    }

    return shouldScroll;
}

bool MarqueeButton::shouldMarquee(const QStyleOptionButton& opt, QRect* outContentRect, int* outTextWidth) const {
    return isEnabled() && textOverflows(opt, outContentRect, outTextWidth);
}

void MarqueeButton::drawElidedText(QPainter& painter, const QStyleOptionButton& opt, const QRect& rect) const {
    const bool enabled = opt.state & QStyle::State_Enabled;
    const int flags = textFlags(opt) | Qt::AlignCenter;
    const QFontMetrics metrics(font());
    const QString text = metrics.elidedText(opt.text, Qt::ElideRight, rect.width(), flags);
    style()->drawItemText(&painter, rect, flags, opt.palette, enabled, text, QPalette::ButtonText);
}

void MarqueeButton::drawMarqueeText(QPainter& painter, const QStyleOptionButton& opt, const QRect& rect, int width) const {
    const int offset = marqueeOffset(width);
    const int cycleWidth = width + marqueeGap_;
    const bool enabled = opt.state & QStyle::State_Enabled;
    const int flags = textFlags(opt) | Qt::AlignLeft;

    painter.save();
    painter.setClipRect(rect);

    QRect first(rect.left() - offset, rect.top(), width, rect.height());
    style()->drawItemText(&painter, first, flags, opt.palette, enabled, opt.text, QPalette::ButtonText);

    QRect second(first.left() + cycleWidth, rect.top(), width, rect.height());
    style()->drawItemText(&painter, second, flags, opt.palette, enabled, opt.text, QPalette::ButtonText);

    painter.restore();
}

void MarqueeButton::updateMarqueeState() {
    if (!hovering_) {
        stopMarqueeTimer();
        return;
    }

    QStyleOptionButton opt;
    initStyleOption(&opt);
    if (!shouldMarquee(opt)) {
        stopMarqueeTimer();
        return;
    }

    if (!marqueeElapsed_.isValid()) {
        marqueeElapsed_.restart();
    }
    if (!marqueeTimer_.isActive()) {
        marqueeTimer_.start(kMarqueeTimerIntervalMs, this);
    }
}

void MarqueeButton::stopMarqueeTimer() {
    if (marqueeTimer_.isActive()) {
        marqueeTimer_.stop();
    }
    marqueeElapsed_.invalidate();
}
