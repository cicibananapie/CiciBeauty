// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "contextbarentrybutton.h"

#include <algorithm>

#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>

#include "marqueelabel.h"
#include "commonui.h"

namespace {

constexpr int kHorizontalMargin = 14;
constexpr int kBrandSpacing = 4;
constexpr int kStatusSpacing = 7;
constexpr int kBrandIconSize = 16;
constexpr int kMinimumButtonWidth = 112;
constexpr int kMaximumButtonWidth = 168;
constexpr int kMinimumButtonHeight = 22;
constexpr int kMaximumStatusWidth = 106;
constexpr int kStatusHorizontalMargin = 8;
constexpr int kStatusHeight = 20;
constexpr int kCornerRadius = 5;

struct ToneColors {
    QColor border;
    QColor text;
    QColor statusBackground;
};

ToneColors colorsForTone(ContextBarEntryButton::Tone tone) {
    switch (tone) {
    case ContextBarEntryButton::Tone::Danger:
        return {QColor(QStringLiteral("#7D4F59")),
                QColor(QStringLiteral("#CF8A94")),
                QColor(QStringLiteral("#49343B"))};
    case ContextBarEntryButton::Tone::Success:
        return {QColor(QStringLiteral("#4D755C")),
                QColor(QStringLiteral("#82BD96")),
                QColor(QStringLiteral("#30463A"))};
    case ContextBarEntryButton::Tone::Trial:
        return {QColor(QStringLiteral("#806C43")),
                QColor(QStringLiteral("#CEB774")),
                QColor(QStringLiteral("#49412F"))};
    case ContextBarEntryButton::Tone::Confirming:
        return {QColor(QStringLiteral("#536B8A")),
                QColor(QStringLiteral("#89A6CF")),
                QColor(QStringLiteral("#344155"))};
    case ContextBarEntryButton::Tone::Neutral:
        return {QColor(QStringLiteral("#596273")),
                QColor(QStringLiteral("#929BA8")),
                QColor(QStringLiteral("#353B47"))};
    }

    return {QColor(QStringLiteral("#596273")),
            QColor(QStringLiteral("#929BA8")),
            QColor(QStringLiteral("#353B47"))};
}

} // namespace

ContextBarEntryButton::ContextBarEntryButton(QWidget* parent)
    : QPushButton(parent) {
    setText({});
    setFocusPolicy(Qt::NoFocus);
    setAutoDefault(false);
    setDefault(false);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMinimumSize(kMinimumButtonWidth, kMinimumButtonHeight);
    setMaximumWidth(kMaximumButtonWidth);

    iconLabel_ = new QLabel(this);
    iconLabel_->setObjectName(QStringLiteral("ciciContextBrandIcon"));
    iconLabel_->setPixmap(ciciPluginIcon().pixmap(kBrandIconSize, kBrandIconSize));
    iconLabel_->setFixedSize(kBrandIconSize, kBrandIconSize);
    iconLabel_->setAlignment(Qt::AlignCenter);
    iconLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);

    brandLabel_ = new QLabel(QStringLiteral("Cici"), this);
    brandLabel_->setObjectName(QStringLiteral("ciciContextBrandLabel"));
    brandLabel_->setStyleSheet(QStringLiteral(
        "color: #CBD1DD; background-color: transparent; border: none;"));
    brandLabel_->setAlignment(Qt::AlignCenter);
    brandLabel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    auto* brandContainer = new QWidget(this);
    brandContainer->setObjectName(QStringLiteral("ciciContextBrandContainer"));
    brandContainer->setAttribute(Qt::WA_TransparentForMouseEvents);
    brandContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* brandLayout = new QHBoxLayout(brandContainer);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(kBrandSpacing);
    brandLayout->addWidget(iconLabel_);
    brandLayout->addWidget(brandLabel_);

    statusContainer_ = new QWidget(this);
    statusContainer_->setObjectName(QStringLiteral("ciciContextStatusContainer"));
    statusContainer_->setAttribute(Qt::WA_StyledBackground, true);
    statusContainer_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    statusContainer_->setFixedHeight(kStatusHeight);
    statusContainer_->setMaximumWidth(kMaximumStatusWidth);

    statusLabel_ = new MarqueeLabel(statusContainer_);
    statusLabel_->setObjectName(QStringLiteral("ciciContextStatusLabel"));
    statusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    statusLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* statusLayout = new QHBoxLayout(statusContainer_);
    statusLayout->setContentsMargins(kStatusHorizontalMargin, 0,
                                     kStatusHorizontalMargin, 0);
    statusLayout->setSpacing(0);
    statusLayout->addWidget(statusLabel_);
    statusContainer_->hide();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(kHorizontalMargin, 0, kHorizontalMargin, 0);
    layout->setSpacing(kStatusSpacing);
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(brandContainer);
    layout->addWidget(statusContainer_);

    setAccessibleName(brandLabel_->text());
    updateStatusColor();
}

void ContextBarEntryButton::followHeightOf(QWidget* widget) {
    if (heightReference_ == widget) {
        syncReferenceHeight();
        return;
    }

    if (heightReference_ != nullptr) {
        heightReference_->removeEventFilter(this);
    }
    heightReference_ = widget;
    heightReference_->installEventFilter(this);
    syncReferenceHeight();
}

void ContextBarEntryButton::setStatus(const QString& text, Tone tone) {
    if (statusLabel_->text() == text && tone_ == tone) {
        return;
    }

    tone_ = tone;
    if (text.isEmpty()) {
        statusLabel_->clear();
        statusContainer_->setMaximumWidth(0);
        statusContainer_->hide();
    }
    else {
        statusLabel_->setText(text);
        statusContainer_->setMaximumWidth(kMaximumStatusWidth);
        statusContainer_->show();
    }
    setAccessibleName(text.isEmpty()
        ? brandLabel_->text()
        : QStringLiteral("%1, %2").arg(brandLabel_->text(), text));
    updateStatusColor();
    updateGeometry();
    update();
}

QString ContextBarEntryButton::statusText() const {
    return statusLabel_->text();
}

ContextBarEntryButton::Tone ContextBarEntryButton::tone() const {
    return tone_;
}

bool ContextBarEntryButton::eventFilter(QObject* watched, QEvent* event) {
    if (watched == heightReference_
        && (event->type() == QEvent::Resize
            || event->type() == QEvent::Show
            || event->type() == QEvent::StyleChange
            || event->type() == QEvent::FontChange)) {
        syncReferenceHeight();
    }

    return QPushButton::eventFilter(watched, event);
}

QSize ContextBarEntryButton::sizeHint() const {
    const int brandWidth = brandLabel_->sizeHint().width();
    int contentWidth = kBrandIconSize + kBrandSpacing + brandWidth;
    if (!statusLabel_->text().isEmpty()) {
        const int statusWidth = QFontMetrics(statusLabel_->font())
            .horizontalAdvance(statusLabel_->text());
        const int statusAreaWidth = statusWidth + 2 * kStatusHorizontalMargin;
        contentWidth += kStatusSpacing
            + (std::min)(statusAreaWidth, kMaximumStatusWidth);
    }

    const int width = (std::min)(contentWidth + 2 * kHorizontalMargin,
                                 kMaximumButtonWidth);
    const int height = (std::max)(QPushButton::sizeHint().height(),
                                  kMinimumButtonHeight);
    return QSize((std::max)(width, kMinimumButtonWidth), height);
}

void ContextBarEntryButton::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    const ToneColors colors = colorsForTone(tone_);
    QColor borderColor = colors.border;
    QColor backgroundTop(QStringLiteral("#303541"));
    QColor backgroundBottom(QStringLiteral("#292E39"));
    if (isDown()) {
        backgroundTop = QColor(QStringLiteral("#232934"));
        backgroundBottom = QColor(QStringLiteral("#303744"));
        borderColor = borderColor.darker(112);
    }
    else if (underMouse()) {
        backgroundTop = QColor(QStringLiteral("#3A4251"));
        backgroundBottom = QColor(QStringLiteral("#2B323F"));
        borderColor = borderColor.lighter(116);
    }

    if (!isEnabled()) {
        borderColor.setAlpha(110);
        backgroundTop.setAlpha(150);
        backgroundBottom.setAlpha(150);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Keep the widget height aligned with the native context-bar buttons, but
    // leave the same vertical breathing room around the visible button frame.
    const QRectF buttonRect = QRectF(rect()).adjusted(0.75, 2.0, -0.75, -2.0);

    // Draw a slightly lowered dark frame first.  The part below buttonRect
    // remains visible after the main body is painted and forms the drop shadow.
    QColor shadowColor(0, 0, 0, isDown() ? 55 : 95);
    painter.setPen(QPen(shadowColor, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(buttonRect.translated(0.0, 1.25),
                            kCornerRadius,
                            kCornerRadius);

    QLinearGradient backgroundGradient(buttonRect.topLeft(), buttonRect.bottomLeft());
    backgroundGradient.setColorAt(0.0, backgroundTop);
    backgroundGradient.setColorAt(1.0, backgroundBottom);

    QLinearGradient borderGradient(buttonRect.topLeft(), buttonRect.bottomLeft());
    borderGradient.setColorAt(0.0, borderColor.lighter(112));
    borderGradient.setColorAt(0.48, borderColor);
    borderGradient.setColorAt(1.0, borderColor.darker(118));

    painter.setPen(QPen(QBrush(borderGradient), 1.0));
    painter.setBrush(backgroundGradient);
    painter.drawRoundedRect(buttonRect,
                            kCornerRadius,
                            kCornerRadius);

    QLinearGradient innerEdge(buttonRect.topLeft(), buttonRect.bottomLeft());
    innerEdge.setColorAt(0.0, QColor(255, 255, 255, 20));
    innerEdge.setColorAt(0.45, QColor(255, 255, 255, 0));
    innerEdge.setColorAt(0.72, QColor(0, 0, 0, 0));
    innerEdge.setColorAt(1.0, QColor(0, 0, 0, 45));
    painter.setPen(QPen(QBrush(innerEdge), 0.75));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(buttonRect.adjusted(1.0, 1.0, -1.0, -1.0),
                            kCornerRadius - 1,
                            kCornerRadius - 1);
}

void ContextBarEntryButton::syncReferenceHeight() {
    if (heightReference_ != nullptr && height() != heightReference_->height()) {
        setFixedHeight(heightReference_->height());
    }
}

void ContextBarEntryButton::updateStatusColor() {
    const ToneColors colors = colorsForTone(tone_);
    const QColor statusTop = colors.statusBackground.lighter(106);
    const QColor statusBottom = colors.statusBackground.darker(108);
    const QColor statusBorder = colors.statusBackground.lighter(118);
    statusLabel_->setStyleSheet(QStringLiteral(
        "color: %1; background-color: transparent; border: none; font-weight: 500;")
        .arg(colors.text.name()));
    statusContainer_->setStyleSheet(QStringLiteral(
        "background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,"
        "                            stop: 0 %1, stop: 0.55 %2, stop: 1 %3);"
        "border: 1px solid %4; border-radius: %5px;")
        .arg(statusTop.name(),
             colors.statusBackground.name(),
             statusBottom.name(),
             statusBorder.name())
        .arg(kStatusHeight / 2));
}
