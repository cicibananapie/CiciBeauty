// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QWidget>

#include "../subscription/subscriptioncontroller.h"

class GlobalModel;
class QLabel;
class QPushButton;
class QStackedLayout;
class QTimer;

class SubscriptionTipWidget : public QWidget {
    Q_OBJECT

public:
    explicit SubscriptionTipWidget(GlobalModel* app, QWidget* parent = nullptr);

private:
    void updateSubscriptionState();

private:
    GlobalModel* app_ = nullptr;
    QLabel* messageLabel_ = nullptr;
};

class PurchasePromptPage : public QWidget {
    Q_OBJECT

public:
    explicit PurchasePromptPage(QWidget* parent = nullptr);
    void setSubscriptionType(SubscriptionType type);

signals:
    void purchaseRequested();

private:
    QLabel* titleLabel_ = nullptr;
    QLabel* descriptionLabel_ = nullptr;
    QLabel* trialOfferLabel_ = nullptr;
    QPushButton* purchaseButton_ = nullptr;
};

class PaymentPendingPage : public QWidget {
    Q_OBJECT

public:
    explicit PaymentPendingPage(QWidget* parent = nullptr);

signals:
    void paymentCompleted();

private:
    QLabel* titleLabel_ = nullptr;
    QLabel* descriptionLabel_ = nullptr;
    QPushButton* completedButton_ = nullptr;
};

class CheckingPage : public QWidget {
    Q_OBJECT

public:
    explicit CheckingPage(QWidget* parent = nullptr);
};

class CheckFailedPage : public QWidget {
    Q_OBJECT

public:
    explicit CheckFailedPage(QWidget* parent = nullptr);

signals:
    void retryRequested();
    void backRequested();
};

class SubscriptionWidget : public QWidget {
    Q_OBJECT

public:
    explicit SubscriptionWidget(GlobalModel* app, QWidget* parent = nullptr);
    static void openSubscriptionPurchasePage();

signals:
    void purchaseRequested();
    void paymentCompleted();

private:
    void beginPurchase();
    void confirmPaymentCompleted();
    void retryChecking();
    void backToPaymentPending();
    void showPage(QWidget* page, const char* pageName, const char* reason);
    const char* currentPageLogName() const;
    void startPaymentPolling(int timeoutMs, const char* reason);
    void stopPaymentPolling(const char* reason);
    void onPaymentTimeout();
    void updateSubscriptionState();

private:
    GlobalModel* app_ = nullptr;
    QStackedLayout* stackLayout_ = nullptr;
    PurchasePromptPage* purchasePromptPage_ = nullptr;
    PaymentPendingPage* paymentPendingPage_ = nullptr;
    CheckingPage* checkingPage_ = nullptr;
    CheckFailedPage* checkFailedPage_ = nullptr;
    QTimer* paymentPollingTimer_ = nullptr;
    QTimer* paymentTimeoutTimer_ = nullptr;
};
