// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "tipswidget.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

TipsWidget::TipsWidget(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("ciciTipsWidget"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(QStringLiteral(
        "QWidget#ciciTipsWidget { background-color: #3B3328; }"
        "QWidget#ciciTipsWidget QLabel { color: #F0DFC6; }"));

    messageLabel_ = new QLabel(this);
    messageLabel_->setTextFormat(Qt::PlainText);
    messageLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    messageLabel_->installEventFilter(this);

    actionButton_ = new QPushButton(this);
    actionButton_->hide();
    connect(actionButton_, &QPushButton::clicked, this, &TipsWidget::actionTriggered);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(8);
    layout->addWidget(messageLabel_, 1);
    layout->addWidget(actionButton_);

    hide();
}

void TipsWidget::showTip(const QString& text, const QString& actionText) {
    message_ = text;
    messageLabel_->setAccessibleName(text);
    actionButton_->setText(actionText);
    actionButton_->setVisible(!actionText.isEmpty());
    updateMessage();
    show();
}

bool TipsWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == messageLabel_ && (event->type() == QEvent::Resize || event->type() == QEvent::FontChange)) {
        updateMessage();
    }
    return QWidget::eventFilter(watched, event);
}

void TipsWidget::updateMessage() {
    const auto text = messageLabel_->fontMetrics().elidedText(
        message_, Qt::ElideRight, messageLabel_->contentsRect().width(), Qt::TextSingleLine);
    messageLabel_->setText(text);
    setToolTip(text == message_ ? QString() : message_);
}
