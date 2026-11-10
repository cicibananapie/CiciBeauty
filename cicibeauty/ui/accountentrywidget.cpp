// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "accountentrywidget.h"

#include <cassert>

#include <QEasingCurve>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "../login/loginlogic.h"
#include "../modelapp.h"
#include "../statistics/settingsanalytics.h"
#include "../subscription/subscriptioncontroller.h"
#include "avatarimagewidget.h"
#include "commonui.h"
#include "marqueelabel.h"
#include "subscriptionuiformatter.h"

namespace {

constexpr int kAccountNameLabelWidth = 130;
constexpr int kCollapsedHeight = 56;
constexpr int kExpandedHeight = 120;
constexpr int kExpansionDurationMs = 160;

} // namespace

AccountEntryWidget::AccountEntryWidget(GlobalModel* app, QWidget* parent)
    : QWidget(parent),
      app_(app) {
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("accountEntryWidget"));
    setMinimumHeight(kCollapsedHeight);
    setMaximumHeight(kCollapsedHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    expansionAnimation_ = new QPropertyAnimation(this, "maximumHeight", this);
    expansionAnimation_->setDuration(kExpansionDurationMs);
    expansionAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(expansionAnimation_, &QPropertyAnimation::finished, this, [this]() {
        if (!expanded_) {
            actionArea_->hide();
        }
    });

    auto* accountInfo = new QWidget(this);
    accountInfo->setFixedHeight(kCollapsedHeight - 2);

    avatar_ = new AvatarImageWidget(app, accountInfo);
    avatar_->setDiameter(32);

    nameLabel_ = createFixWidthMarqueeLabel(QString(), kAccountNameLabelWidth);
    nameLabel_->setParent(accountInfo);
    nameLabel_->setObjectName(QStringLiteral("accountNameLabel"));
    nameLabel_->setTextFormat(Qt::PlainText);
    nameLabel_->setStyleSheet(QStringLiteral(
        "color: #E8EDF5; font-size: 13px; font-weight: 600;"));

    subscriptionLabel_ = new MarqueeLabel(accountInfo);
    subscriptionLabel_->setObjectName(QStringLiteral("accountStatusLabel"));
    subscriptionLabel_->setFixedWidth(kAccountNameLabelWidth);
    subscriptionLabel_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    subscriptionLabel_->setTextFormat(Qt::PlainText);
    subscriptionLabel_->setStyleSheet(
        QStringLiteral("color: #D6B86A; font-size: 11px; font-weight: 500;"));

    auto* textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    textLayout->addWidget(nameLabel_);
    textLayout->addWidget(subscriptionLabel_);

    auto* accountInfoLayout = new QHBoxLayout(accountInfo);
    accountInfoLayout->setContentsMargins(9, 7, 9, 7);
    accountInfoLayout->setSpacing(8);
    accountInfoLayout->addWidget(avatar_);
    accountInfoLayout->addLayout(textLayout, 1);

    actionArea_ = new QWidget(this);
    actionArea_->setFixedHeight(kExpandedHeight - kCollapsedHeight);

    auto createSeparator = [this]() {
        auto* separator = new QFrame(actionArea_);
        separator->setFrameShape(QFrame::HLine);
        separator->setFixedHeight(1);
        separator->setStyleSheet(
            QStringLiteral("background-color: #343C49; border: none;"));
        return separator;
    };

    auto* manageAccountButton = new QPushButton(tr("Manage account"), actionArea_);
    manageAccountButton->setObjectName(QStringLiteral("manageAccountButton"));
    manageAccountButton->setCursor(Qt::PointingHandCursor);
    manageAccountButton->setFixedHeight(28);
    QObject::connect(manageAccountButton, &QPushButton::clicked,
                     this, []() {
                         reportAccountUiAction(AccountUiAction::ManageAccountClick);
                         LoginLogic::openProfilePage();
                     });

    auto* logoutButton = new QPushButton(tr("Log out"), actionArea_);
    logoutButton->setObjectName(QStringLiteral("logoutButton"));
    logoutButton->setCursor(Qt::PointingHandCursor);
    logoutButton->setFixedHeight(28);
    QObject::connect(logoutButton, &QPushButton::clicked,
                     this, [this]() {
                         reportAccountUiAction(AccountUiAction::LogoutClick);
                         app_->loginLogic()->logout();
                     });

    auto* actionLayout = new QVBoxLayout(actionArea_);
    actionLayout->setContentsMargins(0, 0, 0, 6);
    actionLayout->setSpacing(0);
    actionLayout->addWidget(createSeparator());
    actionLayout->addWidget(manageAccountButton);
    actionLayout->addWidget(createSeparator());
    actionLayout->addWidget(logoutButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(accountInfo);
    layout->addWidget(actionArea_);
    actionArea_->hide();

    clearAccount();
    QObject::connect(app_->subscriptionController(),
                     &SubscriptionController::subscriptionInfoUpdated,
                     this,
                     &AccountEntryWidget::updateSubscriptionStatus);
    updateSubscriptionStatus();
    updateStyle();
}

void AccountEntryWidget::setAccount(const QString& avatarUrl, const QString& displayName) {
    assert(app_->loginLogic()->hasUsableSession());
    assert(!app_->subscriptionController()->uid().empty());

    displayName_ = displayName.trimmed();
    if (displayName_.isEmpty()) {
        displayName_ = tr("Cici User");
    }

    avatar_->setFallbackText(displayName_);
    avatar_->setAvatarUrl(avatarUrl);
    nameLabel_->setText(displayName_);
    setCursor(Qt::ArrowCursor);
}

void AccountEntryWidget::clearAccount() {
    setExpanded(false, false);
    displayName_ = tr("Sign in");
    avatar_->clear();
    avatar_->setFallbackText(displayName_);
    nameLabel_->setText(displayName_);
    setCursor(Qt::PointingHandCursor);
}

int AccountEntryWidget::maximumInteractiveHeight() const {
    return kExpandedHeight;
}

void AccountEntryWidget::updateSubscriptionStatus() {
    QString text;
    const auto* login = app_->loginLogic();
    const auto* subscription = app_->subscriptionController();
    if (!login->hasUsableSession()) {
        text = tr("Sign in to Cici Beauty");
    } else if (subscription->uid() == login->uid()) {
        const SubscriptionType type = subscription->subscriptionType();
        if (type == Temporary) {
            text = tr("Confirming membership status");
        }
        else if (type == NotSubscribed) {
            text = tr("Membership is not activated");
        }
        else if (type == Expired) {
            text = tr("Membership has expired");
        }
        else {
            text = tr("%1 membership, expires %2")
                       .arg(SubscriptionUi::membershipTypeName(type),
                            SubscriptionUi::expirationDate(subscription->subscriptionInfo()));
        }
    }
    subscriptionLabel_->setText(text);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void AccountEntryWidget::enterEvent(QEnterEvent* event)
#else
void AccountEntryWidget::enterEvent(QEvent* event)
#endif
{
    QWidget::enterEvent(event);
    hovered_ = true;
    updateStyle();
    if (app_->loginLogic()->hasUsableSession()) {
        setExpanded(true, true);
    }
}

void AccountEntryWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    hovered_ = false;
    updateStyle();
    setExpanded(false, true);
}

void AccountEntryWidget::mouseReleaseEvent(QMouseEvent* event) {
    QWidget::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton
        && !app_->loginLogic()->hasUsableSession()
        && rect().contains(event->pos())) {
        emit loginRequested();
    }
}

void AccountEntryWidget::setExpanded(bool expanded, bool animated) {
    const int targetHeight = expanded ? kExpandedHeight : kCollapsedHeight;
    if (expanded_ == expanded && maximumHeight() == targetHeight) {
        actionArea_->setVisible(expanded);
        return;
    }

    expanded_ = expanded;
    expansionAnimation_->stop();
    if (expanded) {
        actionArea_->show();
    }
    if (!animated) {
        setMaximumHeight(targetHeight);
        actionArea_->setVisible(expanded);
        return;
    }

    expansionAnimation_->setStartValue(maximumHeight());
    expansionAnimation_->setEndValue(targetHeight);
    expansionAnimation_->start();
}

void AccountEntryWidget::updateStyle() {
    const QString background = hovered_ ? QStringLiteral("#2D3440")
                                        : QStringLiteral("#232832");
    const QString border = hovered_ ? QStringLiteral("#79A9FF")
                                    : QStringLiteral("transparent");
    setStyleSheet(QStringLiteral(
        "QWidget#accountEntryWidget {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 8px;"
        "}"
        "QPushButton {"
        "  background-color: transparent;"
        "  border: none;"
        "  border-radius: 0px;"
        "  padding: 0px 9px 0px 49px;"
        "  text-align: left;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "}"
        "QPushButton#manageAccountButton { color: #79A9FF; }"
        "QPushButton#manageAccountButton:hover { background-color: #343C49; }"
        "QPushButton#manageAccountButton:pressed { background-color: #3B4453; }"
        "QPushButton#logoutButton { color: #FF7D83; }"
        "QPushButton#logoutButton:hover { background-color: #343C49; }"
        "QPushButton#logoutButton:pressed { background-color: #3B4453; }")
                      .arg(background, border));
}
