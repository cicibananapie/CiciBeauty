// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "settingaccessoverlay.h"

#include <QVBoxLayout>

#include "subscriptionui.h"

SettingAccessOverlay::SettingAccessOverlay(GlobalModel* app, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("settingAccessOverlay"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget#settingAccessOverlay {"
        "  background-color: rgba(8, 11, 18, 150);"
        "}"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    subscriptionWidget_ = new SubscriptionWidget(app, this);
    layout->addWidget(subscriptionWidget_, 0, Qt::AlignCenter);
    subscriptionWidget_->hide();
}

void SettingAccessOverlay::showLoginMask() {
    subscriptionWidget_->hide();
}

void SettingAccessOverlay::showSubscription() {
    subscriptionWidget_->show();
}
