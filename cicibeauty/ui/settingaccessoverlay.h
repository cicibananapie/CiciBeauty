// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QWidget>

class GlobalModel;
class SubscriptionWidget;

class SettingAccessOverlay : public QWidget {
public:
    SettingAccessOverlay(GlobalModel* app, QWidget* parent = nullptr);
    void showLoginMask();
    void showSubscription();

private:
    SubscriptionWidget* subscriptionWidget_ = nullptr;
};
