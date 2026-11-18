// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "contextbarentrycontroller.h"

#include <cassert>
#include <utility>

#include <obs-frontend-api.h>

#include <QBoxLayout>
#include <QMainWindow>
#include <QObject>
#include <QPushButton>
#include <QVariant>
#include <QWidget>

#include "log.h"
#include "../subscription/subscriptioncontroller.h"
#include "contextbarentrybutton.h"
#include "subscriptionuiformatter.h"

namespace {

constexpr const char *kContextSubContainerName = "contextSubContainer";
constexpr const char *kEmptySpaceName = "emptySpace";
constexpr const char *kSourcePropertiesButtonName = "sourcePropertiesButton";
constexpr const char *kContextEntryButtonName = "ciciBeautyContextEntryButton";

enum class Tone {
    Neutral,
    Danger,
    Success,
    Trial,
    Confirming
};

ContextBarEntryButton::StatusColors colorsForTone(Tone tone) {
    switch (tone) {
    case Tone::Danger:
        return {QColor(QStringLiteral("#7D4F59")),
                QColor(QStringLiteral("#CF8A94")),
                QColor(QStringLiteral("#49343B"))};
    case Tone::Success:
        return {QColor(QStringLiteral("#4D755C")),
                QColor(QStringLiteral("#82BD96")),
                QColor(QStringLiteral("#30463A"))};
    case Tone::Trial:
        return {QColor(QStringLiteral("#806C43")),
                QColor(QStringLiteral("#CEB774")),
                QColor(QStringLiteral("#49412F"))};
    case Tone::Confirming:
        return {QColor(QStringLiteral("#536B8A")),
                QColor(QStringLiteral("#89A6CF")),
                QColor(QStringLiteral("#344155"))};
    case Tone::Neutral:
        break;
    }
    return {QColor(QStringLiteral("#596273")),
            QColor(QStringLiteral("#929BA8")),
            QColor(QStringLiteral("#353B47"))};
}

QMainWindow *obsMainWindow() {
    auto *widget = reinterpret_cast<QWidget *>(obs_frontend_get_main_window());
    return qobject_cast<QMainWindow *>(widget);
}

}  // namespace

ContextBarEntryController::ContextBarEntryController(
    SubscriptionController* subscriptionController,
    ClickHandler onClick)
    : subscriptionController_(subscriptionController),
      onClick_(std::move(onClick)) {
    assert(subscriptionController_ != nullptr);
}

ContextBarEntryController::~ContextBarEntryController() {
    detach();
}

bool ContextBarEntryController::attach() {
    if (button_ != nullptr) {
        return true;
    }

    mainWindow_ = obsMainWindow();
    if (mainWindow_ == nullptr) {
        LogWarn << "ContextBarEntryController.attach: OBS main window is null" << logEnd;
        return false;
    }

    if (auto *existingButton = mainWindow_->findChild<ContextBarEntryButton *>(kContextEntryButtonName);
        existingButton != nullptr) {
        button_ = existingButton;
        bindSubscriptionState(existingButton);
        return true;
    }

    auto *contextSubContainer = mainWindow_->findChild<QWidget *>(kContextSubContainerName);
    if (contextSubContainer == nullptr) {
        LogWarn << "ContextBarEntryController.attach: cannot find contextSubContainer" << logEnd;
        return false;
    }

    auto *emptySpace = mainWindow_->findChild<QWidget *>(kEmptySpaceName);
    if (emptySpace == nullptr) {
        LogWarn << "ContextBarEntryController.attach: cannot find emptySpace" << logEnd;
        return false;
    }

    auto *layout = qobject_cast<QBoxLayout *>(contextSubContainer->layout());
    if (layout == nullptr) {
        LogWarn << "ContextBarEntryController.attach: contextSubContainer has no QBoxLayout" << logEnd;
        return false;
    }

    auto *button = createButton(contextSubContainer);
    int insertIndex = layout->indexOf(emptySpace);
    if (insertIndex < 0) {
        insertIndex = layout->count();
    }
    layout->insertWidget(insertIndex, button);
    button_ = button;
    LogInfo << "ContextBarEntryController.attach: context bar entry added" << logEnd;
    return true;
}

void ContextBarEntryController::detach() {
    if (button_ != nullptr) {
        delete button_;
        button_ = nullptr;
    }
    mainWindow_ = nullptr;
}

void ContextBarEntryController::setRecoveryState(LumaRecoveryUiState state) {
    if (recoveryState_ == state) {
        return;
    }
    recoveryState_ = state;
    if (button_ != nullptr) {
        updateStatus(button_);
    }
}

ContextBarEntryButton *ContextBarEntryController::createButton(QWidget *parent) const {
    auto *button = new ContextBarEntryButton(parent);
    button->setObjectName(kContextEntryButtonName);
    button->setToolTip(QObject::tr("Open Cici Beauty"));

    auto *referenceButton =
        mainWindow_ != nullptr ? mainWindow_->findChild<QPushButton *>(kSourcePropertiesButtonName) : nullptr;
    if (referenceButton != nullptr) {
        QSizePolicy sizePolicy = referenceButton->sizePolicy();
        sizePolicy.setHorizontalPolicy(QSizePolicy::Fixed);
        button->setSizePolicy(sizePolicy);
        button->setFont(referenceButton->font());
        button->followHeightOf(referenceButton);

        const QVariant klass = referenceButton->property("class");
        if (klass.isValid()) {
            button->setProperty("class", klass);
        }

        const QVariant themeId = referenceButton->property("themeID");
        if (themeId.isValid()) {
            button->setProperty("themeID", themeId);
        }
    }

    QObject::connect(button, &QPushButton::clicked, button, [handler = onClick_]() {
        if (handler) {
            handler();
        }
    });
    bindSubscriptionState(button);
    return button;
}

void ContextBarEntryController::bindSubscriptionState(ContextBarEntryButton* button) const {
    QObject::connect(subscriptionController_,
                     &SubscriptionController::subscriptionInfoUpdated,
                     button,
                     [this, button]() {
                         updateStatus(button);
                     });
    QObject::connect(subscriptionController_,
                     &SubscriptionController::aboutToExpire,
                     button,
                     [this, button](SubscriptionType, uint32_t) {
                         updateStatus(button);
                     });
    updateStatus(button);
}

void ContextBarEntryController::updateStatus(ContextBarEntryButton* button) const {
    switch (recoveryState_) {
    case LumaRecoveryUiState::Recovering:
        button->setStatus(QObject::tr("Recovering"), colorsForTone(Tone::Confirming));
        button->setToolTip(QObject::tr("Recovering the CiciBeauty service"));
        return;
    case LumaRecoveryUiState::Paused:
        button->setStatus(QObject::tr("Unavailable"), colorsForTone(Tone::Danger));
        button->setToolTip(QObject::tr("CiciBeauty is temporarily unavailable"));
        return;
    case LumaRecoveryUiState::None:
        break;
    }

    QString statusText;
    QString tooltip = QObject::tr("Open Cici Beauty");
    Tone tone = Tone::Neutral;

    if (subscriptionController_->uid().empty()) {
        statusText = QObject::tr("Not signed in");
        tooltip = QObject::tr("Sign in to Cici Beauty");
    }
    else {
        const SubscriptionType type = subscriptionController_->subscriptionType();
        if (type == Temporary) {
            statusText = QObject::tr("Confirming membership status");
            tooltip = QObject::tr("Confirming membership status");
            tone = Tone::Confirming;
        }
        else if (type == NotSubscribed) {
            statusText = QObject::tr("Not subscribed");
            tooltip = QObject::tr("Cici Beauty membership is not activated");
            tone = Tone::Danger;
        }
        else if (type == Expired) {
            statusText = QObject::tr("Membership expired");
            tooltip = QObject::tr("Cici Beauty membership has expired");
            tone = Tone::Danger;
        }
        else if (type == Trial && subscriptionController_->isVip()) {
            statusText = QObject::tr("Trial · %1 days")
                .arg(subscriptionController_->howManyDaysBeforeExpire());
            tooltip = QObject::tr("%1 membership, expires %2")
                .arg(SubscriptionUi::membershipTypeName(type),
                     SubscriptionUi::expirationDate(subscriptionController_->subscriptionInfo()));
            tone = Tone::Trial;
        }
        else if (subscriptionController_->isVip()) {
            statusText = QStringLiteral("VIP");
            tooltip = QObject::tr("%1 membership, expires %2")
                .arg(SubscriptionUi::membershipTypeName(type),
                     SubscriptionUi::expirationDate(subscriptionController_->subscriptionInfo()));
            tone = Tone::Success;
        }
    }

    button->setStatus(statusText, colorsForTone(tone));
    button->setToolTip(tooltip);
}
