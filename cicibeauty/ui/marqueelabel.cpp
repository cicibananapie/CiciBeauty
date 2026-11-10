// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "marqueelabel.h"

#include <algorithm>

#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QTextDocument>
#include <QTimerEvent>

namespace {
constexpr int kMarqueeTimerIntervalMs = 16;
constexpr int kMinimumMarqueeSpeed = 1;
}

MarqueeLabel::MarqueeLabel(QWidget* parent)
    : QLabel(parent) {
}

MarqueeLabel::MarqueeLabel(const QString& text, QWidget* parent)
    : QLabel(text, parent) {
}

MarqueeLabel::~MarqueeLabel() {
    stopMarqueeTimer();
}

void MarqueeLabel::setMarqueeSpeed(int pxPerSecond) {
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

int MarqueeLabel::marqueeSpeed() const {
    return marqueeSpeed_;
}

void MarqueeLabel::setMarqueeGap(int px) {
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

int MarqueeLabel::marqueeGap() const {
    return marqueeGap_;
}

void MarqueeLabel::setMarqueeStartDelay(int ms) {
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

int MarqueeLabel::marqueeStartDelay() const {
    return marqueeStartDelay_;
}

void MarqueeLabel::paintEvent(QPaintEvent* event) {
    QRect rect;
    int width = 0;
    if (!textOverflows(&rect, &width)) {
        QLabel::paintEvent(event);
        return;
    }

    QPainter painter(this);
    drawFrame(painter);

    if (!hovering_ || !shouldMarquee(&rect, &width)) {
        drawElidedText(painter, rect);
        return;
    }

    updateMarqueeState();
    drawMarqueeText(painter, rect, width);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void MarqueeLabel::enterEvent(QEnterEvent* event) {
#else
void MarqueeLabel::enterEvent(QEvent* event) {
#endif
    QLabel::enterEvent(event);
    hovering_ = true;
    marqueeElapsed_.restart();
    updateMarqueeState();
    update();
}

void MarqueeLabel::leaveEvent(QEvent* event) {
    QLabel::leaveEvent(event);
    hovering_ = false;
    stopMarqueeTimer();
    update();
}

void MarqueeLabel::timerEvent(QTimerEvent* event) {
    if (event->timerId() != marqueeTimer_.timerId()) {
        QLabel::timerEvent(event);
        return;
    }

    if (!hovering_ || !shouldMarquee()) {
        stopMarqueeTimer();
    }

    update();
}

void MarqueeLabel::hideEvent(QHideEvent* event) {
    hovering_ = false;
    stopMarqueeTimer();
    QLabel::hideEvent(event);
}

void MarqueeLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    updateMarqueeState();
}

void MarqueeLabel::changeEvent(QEvent* event) {
    QLabel::changeEvent(event);

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

bool MarqueeLabel::canDrawSingleLineText() const {
    const Qt::TextInteractionFlags interactionFlags = textInteractionFlags();
    if (text().isEmpty() || wordWrap()
        || interactionFlags.testFlag(Qt::TextSelectableByMouse)
        || interactionFlags.testFlag(Qt::TextSelectableByKeyboard)) {
        return false;
    }

    const Qt::TextFormat format = textFormat();
    if (format == Qt::RichText) {
        return false;
    }
    if (format == Qt::AutoText && Qt::mightBeRichText(text())) {
        return false;
    }

    return true;
}

QRect MarqueeLabel::textRect() const {
    QStyleOptionFrame opt;
    initStyleOption(&opt);

    QRect rect = style()->subElementRect(QStyle::SE_FrameContents, &opt, this);
    const int labelMargin = margin();
    rect.adjust(labelMargin, labelMargin, -labelMargin, -labelMargin);

    const int indent = effectiveIndent();
    if (indent > 0) {
        const Qt::Alignment visualAlignment = QStyle::visualAlignment(layoutDirection(), alignment());
        if (visualAlignment & Qt::AlignLeft) {
            rect.setLeft(rect.left() + indent);
        } else if (visualAlignment & Qt::AlignRight) {
            rect.setRight(rect.right() - indent);
        }
    }

    return rect;
}

int MarqueeLabel::effectiveIndent() const {
    if (indent() >= 0) {
        return indent();
    }

    if (frameWidth() <= 0) {
        return 0;
    }

    return QFontMetrics(font()).horizontalAdvance(QLatin1Char('x')) / 2;
}

int MarqueeLabel::textWidth() const {
    const QFontMetrics metrics(font());
    return metrics.size(textFlags(), text()).width();
}

int MarqueeLabel::textFlags() const {
    int flags = Qt::TextSingleLine | static_cast<int>(alignment());
    if ((flags & Qt::AlignVertical_Mask) == 0) {
        flags |= Qt::AlignVCenter;
    }
    if ((flags & Qt::AlignHorizontal_Mask) == 0) {
        flags |= Qt::AlignLeft;
    }

    if (style()->styleHint(QStyle::SH_UnderlineShortcut, nullptr, this)) {
        flags |= Qt::TextShowMnemonic;
    } else {
        flags |= Qt::TextHideMnemonic;
    }

    return flags;
}

int MarqueeLabel::marqueeOffset(int width) const {
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

bool MarqueeLabel::textOverflows(QRect* outTextRect, int* outTextWidth) const {
    if (!canDrawSingleLineText()) {
        return false;
    }

    const QRect rect = textRect();
    const int width = textWidth();
    const bool shouldScroll = rect.width() > 0 && width > rect.width();
    if (outTextRect != nullptr) {
        *outTextRect = rect;
    }
    if (outTextWidth != nullptr) {
        *outTextWidth = width;
    }

    return shouldScroll;
}

bool MarqueeLabel::shouldMarquee(QRect* outTextRect, int* outTextWidth) const {
    return isEnabled() && textOverflows(outTextRect, outTextWidth);
}

void MarqueeLabel::drawFrame(QPainter& painter) const {
    if (frameShape() == QFrame::NoFrame) {
        return;
    }

    QStyleOptionFrame opt;
    initStyleOption(&opt);
    style()->drawControl(QStyle::CE_ShapedFrame, &opt, &painter, this);
}

void MarqueeLabel::drawElidedText(QPainter& painter, const QRect& rect) const {
    const int flags = textFlags();
    const QFontMetrics metrics(font());
    const QString elided = metrics.elidedText(text(), Qt::ElideRight, rect.width(), flags);
    style()->drawItemText(&painter, rect, flags, palette(), isEnabled(), elided, foregroundRole());
}

void MarqueeLabel::drawMarqueeText(QPainter& painter, const QRect& rect, int width) const {
    const int offset = marqueeOffset(width);
    const int cycleWidth = width + marqueeGap_;
    const int flags = (textFlags() & ~Qt::AlignHorizontal_Mask) | Qt::AlignLeft;

    painter.save();
    painter.setClipRect(rect);

    QRect first(rect.left() - offset, rect.top(), width, rect.height());
    style()->drawItemText(&painter, first, flags, palette(), isEnabled(), text(), foregroundRole());

    QRect second(first.left() + cycleWidth, rect.top(), width, rect.height());
    style()->drawItemText(&painter, second, flags, palette(), isEnabled(), text(), foregroundRole());

    painter.restore();
}

void MarqueeLabel::updateMarqueeState() {
    if (!hovering_) {
        stopMarqueeTimer();
        return;
    }

    if (!shouldMarquee()) {
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

void MarqueeLabel::stopMarqueeTimer() {
    if (marqueeTimer_.isActive()) {
        marqueeTimer_.stop();
    }
    marqueeElapsed_.invalidate();
}
