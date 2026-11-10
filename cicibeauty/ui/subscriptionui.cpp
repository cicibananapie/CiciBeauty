// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "subscriptionui.h"

#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

#include "log.h"
#include "../modelapp.h"
#include "../qstringconv.h"
#include "../statistics/subscriptionanalytics.h"
#include "subscriptionuiformatter.h"

namespace {

constexpr int kPaymentPollingIntervalMs = 10 * 1000;
constexpr int kPaymentPendingTimeoutMs = 10 * 60 * 1000;
constexpr int kPaymentCheckingTimeoutMs = 2 * 60 * 1000;

bool isConfirmedSubscription(SubscriptionType type) {
    return type != NotSubscribed && type != Expired && type != Temporary;
}

const char* subscriptionTypeLogName(SubscriptionType type) {
    switch (type) {
    case NotSubscribed:
        return "NotSubscribed";
    case Trial:
        return "Trial";
    case Temporary:
        return "Temporary";
    case Monthly:
        return "Monthly";
    case OneMonth:
        return "OneMonth";
    case Yearly:
        return "Yearly";
    case OneYear:
        return "OneYear";
    case Expired:
        return "Expired";
    }

    return "Unknown";
}

QLabel* createPageTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setStyleSheet(QStringLiteral(
        "font-size: 22px; font-weight: 600; color: #F7F3EA;"
        "border: none; background-color: transparent;"));
    return label;
}

QLabel* createPageDescription(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: #BFC4CF;"
        "border: none; background-color: transparent;"));
    return label;
}

QPushButton* createPageButton(const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setMinimumWidth(132);
    button->setFixedHeight(42);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0,"
        "                              stop: 0 #8A6728, stop: 1 #674A18);"
        "  color: #FFF7DC;"
        "  border: 1px solid #B08A43;"
        "  border-radius: 5px;"
        "  padding: 0 22px;"
        "  font-size: 14px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0,"
        "                              stop: 0 #9B7733, stop: 1 #765720);"
        "  border-color: #C9A45B;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #5C4217;"
        "  border-color: #947238;"
        "}"));
    return button;
}

} // namespace

SubscriptionTipWidget::SubscriptionTipWidget(GlobalModel* app, QWidget* parent)
    : QWidget(parent), app_(app) {
    setObjectName(QStringLiteral("subscriptionTipWidget"));
    setFixedHeight(52);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(QStringLiteral(
        "QWidget#subscriptionTipWidget {"
        "  background-color: #332D1F;"
        "  border: 1px solid #6B5830;"
        "  border-radius: 4px;"
        "}"));

    messageLabel_ = new QLabel(this);
    messageLabel_->setStyleSheet(QStringLiteral(
        "color: #F3D38A; border: none; background-color: transparent;"));

    auto* renewButton = new QPushButton(tr("Renew Now"), this);
    renewButton->setCursor(Qt::PointingHandCursor);
    renewButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  color: #58C7FF;"
        "  background-color: transparent;"
        "  border: none;"
        "  padding: 4px 8px;"
        "}"
        "QPushButton:hover {"
        "  color: #8AD8FF;"
        "}"));

    auto* closeButton = new QPushButton(QStringLiteral("\u00d7"), this);
    closeButton->setToolTip(tr("Close"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedSize(28, 28);
    closeButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  color: #B8BEC9;"
        "  background-color: transparent;"
        "  border: none;"
        "  font-size: 18px;"
        "}"
        "QPushButton:hover {"
        "  color: #FFFFFF;"
        "}"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 8, 0);
    layout->setSpacing(8);
    layout->addWidget(messageLabel_);
    layout->addStretch();
    layout->addWidget(renewButton);
    layout->addWidget(closeButton);

    auto* controller = app_->subscriptionController();
    QObject::connect(controller,
                     &SubscriptionController::subscriptionInfoUpdated,
                     this,
                     &SubscriptionTipWidget::updateSubscriptionState);
    QObject::connect(controller,
                     &SubscriptionController::aboutToExpire,
                     this,
                     &SubscriptionTipWidget::updateSubscriptionState);
    QObject::connect(controller,
                     &SubscriptionController::expired,
                     this,
                     &SubscriptionTipWidget::updateSubscriptionState);
    QObject::connect(renewButton, &QPushButton::clicked,
                     this, []() {
                         reportSubscriptionUiAction(
                             SubscriptionUiAction::PurchaseClick,
                             SubscriptionUiSource::ExpirationTip,
                             SubscriptionPurchaseIntent::Renew);
                         SubscriptionWidget::openSubscriptionPurchasePage();
                     });
    QObject::connect(closeButton, &QPushButton::clicked,
                     this, [this]() {
                         reportSubscriptionUiAction(
                             SubscriptionUiAction::TipCloseClick,
                             SubscriptionUiSource::ExpirationTip);
                         hide();
                     });

    updateSubscriptionState();
}

void SubscriptionTipWidget::updateSubscriptionState() {
    auto* controller = app_->subscriptionController();
    const SubscriptionType type = controller->subscriptionType();
    if (!controller->isVip() || type == Temporary) {
        hide();
        return;
    }

    const uint32_t days = controller->howManyDaysBeforeExpire();
    if (days == 0 || days > 7) {
        hide();
        return;
    }

    const QString typeName = SubscriptionUi::membershipTypeName(type);
    if (days == 1) {
        messageLabel_->setText(
            tr("Your %1 membership expires tomorrow.").arg(typeName));
    }
    else {
        messageLabel_->setText(
            tr("Your %1 membership expires in %2 days.")
                .arg(typeName)
                .arg(days));
    }
    show();
}

PurchasePromptPage::PurchasePromptPage(QWidget* parent)
    : QWidget(parent) {
    auto* badgeLabel = new QLabel(QStringLiteral("Cici VIP"), this);
    badgeLabel->setAlignment(Qt::AlignCenter);
    badgeLabel->setFixedSize(144, 30);
    badgeLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #E8D49B;"
        "  background-color: #3A382F;"
        "  border: 1px solid #85744D;"
        "  border-radius: 15px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"));

    titleLabel_ = createPageTitle({}, this);
    descriptionLabel_ = createPageDescription({}, this);
    trialOfferLabel_ = new QLabel(
        tr("New users can enjoy a free 7-day trial"), this);
    trialOfferLabel_->setObjectName(QStringLiteral("newUserTrialOfferLabel"));
    trialOfferLabel_->setAlignment(Qt::AlignCenter);
    trialOfferLabel_->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Preferred);
    trialOfferLabel_->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: #F3D38A;"
        "border: none; background-color: transparent;"));
    trialOfferLabel_->hide();
    purchaseButton_ = createPageButton({}, this);

    auto* footerLabel = new QLabel(
        tr("Your live output is not affected. Settings are temporarily unavailable."),
        this);
    footerLabel->setAlignment(Qt::AlignCenter);
    footerLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    footerLabel->setWordWrap(true);
    footerLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: #858B98;"
        "border: none; background-color: transparent;"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 27, 20, 22);
    layout->setSpacing(0);
    layout->addWidget(badgeLabel, 0, Qt::AlignHCenter);
    layout->addSpacing(13);
    layout->addWidget(titleLabel_);
    layout->addSpacing(9);
    layout->addWidget(descriptionLabel_);
    layout->addSpacing(10);
    layout->addWidget(trialOfferLabel_);
    layout->addSpacing(8);
    layout->addWidget(purchaseButton_, 0, Qt::AlignHCenter);
    layout->addSpacing(15);
    layout->addWidget(footerLabel);

    QObject::connect(purchaseButton_, &QPushButton::clicked,
                     this, &PurchasePromptPage::purchaseRequested);
}

void PurchasePromptPage::setSubscriptionType(SubscriptionType type) {
    if (type == NotSubscribed) {
        trialOfferLabel_->show();
        titleLabel_->setText(tr("Membership Not Activated"));
        descriptionLabel_->setText(
            tr("Subscribe to use advanced beauty tools, AI effects, effect assets, and saved presets."));
        purchaseButton_->setText(tr("Subscribe Now"));
        return;
    }

    if (type == Expired) {
        trialOfferLabel_->hide();
        titleLabel_->setText(tr("Membership Expired"));
        descriptionLabel_->setText(
            tr("Renew to continue using advanced beauty tools, AI effects, effect assets, and saved presets."));
        purchaseButton_->setText(tr("Renew Now"));
    }
}

PaymentPendingPage::PaymentPendingPage(QWidget* parent)
    : QWidget(parent) {
    titleLabel_ = createPageTitle(tr("Complete Your Purchase"), this);
    descriptionLabel_ = createPageDescription(
        tr("Complete the purchase in your browser, then return here to confirm."),
        this);
    completedButton_ = createPageButton(tr("I've Completed Payment"), this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 30, 20, 30);
    layout->setSpacing(16);
    layout->addStretch();
    layout->addWidget(titleLabel_);
    layout->addWidget(descriptionLabel_);
    layout->addSpacing(12);
    layout->addWidget(completedButton_, 0, Qt::AlignHCenter);
    layout->addStretch();

    QObject::connect(completedButton_, &QPushButton::clicked,
                     this, &PaymentPendingPage::paymentCompleted);
}

CheckingPage::CheckingPage(QWidget* parent)
    : QWidget(parent) {
    auto* titleLabel = createPageTitle(tr("Confirming Membership Status"), this);
    auto* descriptionLabel = createPageDescription(tr("Please wait..."), this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 30, 20, 30);
    layout->setSpacing(16);
    layout->addStretch();
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addStretch();
}

CheckFailedPage::CheckFailedPage(QWidget* parent)
    : QWidget(parent) {
    auto* titleLabel = createPageTitle(
        tr("Unable to Confirm Membership Status"), this);
    auto* descriptionLabel = createPageDescription(
        tr("Check your network connection or try again later."), this);
    auto* retryButton = createPageButton(tr("Check Again"), this);
    auto* backButton = new QPushButton(tr("Back"), this);
    backButton->setFixedSize(260, 36);
    backButton->setCursor(Qt::PointingHandCursor);
    backButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #B8BEC9;"
        "  border: none;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover {"
        "  color: #FFFFFF;"
        "}"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 24, 20, 24);
    layout->setSpacing(12);
    layout->addStretch();
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addSpacing(8);
    layout->addWidget(retryButton, 0, Qt::AlignHCenter);
    layout->addWidget(backButton, 0, Qt::AlignHCenter);
    layout->addStretch();

    QObject::connect(retryButton, &QPushButton::clicked,
                     this, &CheckFailedPage::retryRequested);
    QObject::connect(backButton, &QPushButton::clicked,
                     this, &CheckFailedPage::backRequested);
}

SubscriptionWidget::SubscriptionWidget(GlobalModel* app, QWidget* parent)
    : QWidget(parent), app_(app) {
    setFixedSize(420, 276);
    setObjectName(QStringLiteral("subscriptionWidget"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget#subscriptionWidget {"
        "  background-color: #252A36;"
        "  border: 1px solid #71664F;"
        "  border-radius: 10px;"
        "}"));

    stackLayout_ = new QStackedLayout(this);
    stackLayout_->setContentsMargins(0, 0, 0, 0);

    purchasePromptPage_ = new PurchasePromptPage(this);
    paymentPendingPage_ = new PaymentPendingPage(this);
    checkingPage_ = new CheckingPage(this);
    checkFailedPage_ = new CheckFailedPage(this);
    paymentPollingTimer_ = new QTimer(this);
    paymentTimeoutTimer_ = new QTimer(this);
    paymentPollingTimer_->setInterval(kPaymentPollingIntervalMs);
    paymentTimeoutTimer_->setSingleShot(true);

    stackLayout_->addWidget(purchasePromptPage_);
    stackLayout_->addWidget(paymentPendingPage_);
    stackLayout_->addWidget(checkingPage_);
    stackLayout_->addWidget(checkFailedPage_);
    stackLayout_->setCurrentWidget(purchasePromptPage_);
    LogInfo << "SubscriptionWidget: page=PurchasePrompt reason=initialized"
            << logEnd;

    QObject::connect(purchasePromptPage_, &PurchasePromptPage::purchaseRequested,
                     this, &SubscriptionWidget::beginPurchase);
    QObject::connect(paymentPendingPage_, &PaymentPendingPage::paymentCompleted,
                     this, &SubscriptionWidget::confirmPaymentCompleted);
    QObject::connect(checkFailedPage_, &CheckFailedPage::retryRequested,
                     this, &SubscriptionWidget::retryChecking);
    QObject::connect(checkFailedPage_, &CheckFailedPage::backRequested,
                     this, &SubscriptionWidget::backToPaymentPending);
    QObject::connect(paymentPollingTimer_, &QTimer::timeout,
                     app_->subscriptionController(), &SubscriptionController::refresh);
    QObject::connect(paymentTimeoutTimer_, &QTimer::timeout,
                     this, &SubscriptionWidget::onPaymentTimeout);
    QObject::connect(app_->subscriptionController(),
                     &SubscriptionController::subscriptionInfoUpdated,
                     this, &SubscriptionWidget::updateSubscriptionState);
    updateSubscriptionState();
}

void SubscriptionWidget::openSubscriptionPurchasePage() {
    SubscriptionController::openPurchasePage();
}

void SubscriptionWidget::beginPurchase() {
    const SubscriptionPurchaseIntent purchaseIntent =
        app_->subscriptionController()->subscriptionType() == Expired
            ? SubscriptionPurchaseIntent::Renew
            : SubscriptionPurchaseIntent::Subscribe;
    reportSubscriptionUiAction(SubscriptionUiAction::PurchaseClick,
                               SubscriptionUiSource::AccessOverlay,
                               purchaseIntent);
    openSubscriptionPurchasePage();
    showPage(paymentPendingPage_, "PaymentPending", "purchase_requested");
    startPaymentPolling(kPaymentPendingTimeoutMs, "purchase_requested");
    emit purchaseRequested();
}

void SubscriptionWidget::confirmPaymentCompleted() {
    reportSubscriptionUiAction(SubscriptionUiAction::PaymentCompletedClick,
                               SubscriptionUiSource::AccessOverlay);
    showPage(checkingPage_, "Checking", "user_reported_payment_completed");
    startPaymentPolling(kPaymentCheckingTimeoutMs,
                        "user_reported_payment_completed");
    app_->subscriptionController()->refresh();
    emit paymentCompleted();
}

void SubscriptionWidget::retryChecking() {
    reportSubscriptionUiAction(SubscriptionUiAction::CheckRetryClick,
                               SubscriptionUiSource::AccessOverlay);
    showPage(checkingPage_, "Checking", "retry_requested");
    startPaymentPolling(kPaymentCheckingTimeoutMs, "retry_requested");
    app_->subscriptionController()->refresh();
}

void SubscriptionWidget::backToPaymentPending() {
    reportSubscriptionUiAction(SubscriptionUiAction::CheckBackClick,
                               SubscriptionUiSource::AccessOverlay);
    showPage(paymentPendingPage_, "PaymentPending", "back_requested");
    startPaymentPolling(kPaymentPendingTimeoutMs, "back_requested");
}

void SubscriptionWidget::showPage(QWidget* page,
                                  const char* pageName,
                                  const char* reason) {
    const char* previousPageName = currentPageLogName();
    if (stackLayout_->currentWidget() == page) {
        return;
    }

    stackLayout_->setCurrentWidget(page);
    LogInfo << "SubscriptionWidget: page transition from=" << previousPageName
            << " to=" << pageName << " reason=" << reason << logEnd;
}

const char* SubscriptionWidget::currentPageLogName() const {
    const QWidget* currentPage = stackLayout_->currentWidget();
    if (currentPage == purchasePromptPage_) {
        return "PurchasePrompt";
    }
    if (currentPage == paymentPendingPage_) {
        return "PaymentPending";
    }
    if (currentPage == checkingPage_) {
        return "Checking";
    }
    if (currentPage == checkFailedPage_) {
        return "CheckFailed";
    }

    return "None";
}

void SubscriptionWidget::startPaymentPolling(int timeoutMs, const char* reason) {
    paymentPollingTimer_->start();
    paymentTimeoutTimer_->start(timeoutMs);
    LogInfo << "SubscriptionWidget: payment polling started reason=" << reason
            << " intervalMs=" << paymentPollingTimer_->interval()
            << " timeoutMs=" << timeoutMs << logEnd;
}

void SubscriptionWidget::stopPaymentPolling(const char* reason) {
    const bool pollingWasActive = paymentPollingTimer_->isActive();
    const bool timeoutWasActive = paymentTimeoutTimer_->isActive();
    paymentPollingTimer_->stop();
    paymentTimeoutTimer_->stop();
    if (!pollingWasActive && !timeoutWasActive) {
        return;
    }

    LogInfo << "SubscriptionWidget: payment polling stopped reason=" << reason
            << " pollingWasActive=" << pollingWasActive
            << " timeoutWasActive=" << timeoutWasActive << logEnd;
}

void SubscriptionWidget::onPaymentTimeout() {
    if (stackLayout_->currentWidget() == checkingPage_) {
        stopPaymentPolling("checking_timeout");
        showPage(checkFailedPage_, "CheckFailed", "checking_timeout");
        return;
    }

    stopPaymentPolling("payment_pending_timeout");
}

void SubscriptionWidget::updateSubscriptionState() {
    const SubscriptionType type = app_->subscriptionController()->subscriptionType();
    LogInfo << "SubscriptionWidget: subscription state updated type="
            << subscriptionTypeLogName(type)
            << " confirmed=" << isConfirmedSubscription(type) << logEnd;
    if (isConfirmedSubscription(type)) {
        stopPaymentPolling("subscription_confirmed");
        showPage(purchasePromptPage_, "PurchasePrompt", "subscription_confirmed");
        return;
    }

    if (type != NotSubscribed && type != Expired) {
        return;
    }

    purchasePromptPage_->setSubscriptionType(type);
}
