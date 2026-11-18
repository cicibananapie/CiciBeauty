// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <functional>

#include <QPointer>

#include "../lumarecoveryuistate.h"

class QMainWindow;
class QWidget;
class ContextBarEntryButton;
class SubscriptionController;

class ContextBarEntryController {
public:
    using ClickHandler = std::function<void()>;

    ContextBarEntryController(SubscriptionController* subscriptionController,
                              ClickHandler onClick);
    ~ContextBarEntryController();

    bool attach();
    void detach();
    void setRecoveryState(LumaRecoveryUiState state);

private:
    ContextBarEntryButton *createButton(QWidget *parent) const;
    void bindSubscriptionState(ContextBarEntryButton* button) const;
    void updateStatus(ContextBarEntryButton* button) const;

    SubscriptionController* subscriptionController_ = nullptr;
    ClickHandler onClick_;
    LumaRecoveryUiState recoveryState_ = LumaRecoveryUiState::None;
    QPointer<QMainWindow> mainWindow_;
    QPointer<ContextBarEntryButton> button_;
};
