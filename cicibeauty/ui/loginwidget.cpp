// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>

#include <QCloseEvent>
#include <QWindow>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "loginwidget.h"
#include "assetsmgr.h"
#include "commonui.h"
#include "../modelapp.h"
#include "misc.h"
#include "../login/loginlogic.h"
#include "../qstringconv.h"


LoginWidget::LoginWidget(GlobalModel* app,
                         LoginUiSurface surface,
                         QWidget* parent)
    : QWidget(parent), app_(app), surface_(surface) {
	createUI();
}

LoginWidget::~LoginWidget() {
    
}

void LoginWidget::createUI() {
    auto s = app_->loginLogic()->state();
    const bool settingsOverlay =
        surface_ == LoginUiSurface::SettingsOverlay;

    QObject::connect(app_->loginLogic(), &LoginLogic::loginReady, this, [this](LoginReadySource, const std::u8string&) {
        onLoginSuccess();
    });
    QObject::connect(app_->loginLogic(), &LoginLogic::loginFailed, this, &LoginWidget::onLoginFailed);
    setWindowTitle(tr("Cici Beauty"));
    applyCiciWindowIcon(this);
    setObjectName(QStringLiteral("ciciLoginRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedSize(settingsOverlay ? QSize(336, 176) : QSize(360, 220));
    setStyleSheet(settingsOverlay
        ? QStringLiteral(
            "QWidget#ciciLoginRoot {"
            "  background-color: #1b2029;"
            "  color: #eeeeee;"
            "  border: 1px solid #343b48;"
            "  border-radius: 6px;"
            "}")
        : QStringLiteral(
            "QWidget#ciciLoginRoot {"
            "  background-color: #1e1e1e;"
            "  color: #eeeeee;"
            "  border: none;"
            "}"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(settingsOverlay ? 24 : 32,
                               settingsOverlay ? 18 : 26,
                               settingsOverlay ? 24 : 32,
                               settingsOverlay ? 18 : 26);
    layout->setSpacing(0);
    layout->addStretch();

    QLabel* titleLabel = new QLabel(tr("Sign in to Cici Beauty"), this);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 16px;"
        "font-weight: 600;"
        "color: #f1f3f7;"
        "background: transparent;"
        "border: none;"));
    titleLabel->setMinimumHeight(24);
    titleLabel->setAlignment(Qt::AlignCenter);

    auto* titleLayout = new QHBoxLayout;
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(8);
    titleLayout->addStretch();
    auto* logoLabel = new QLabel(this);
    auto img = app_->assets()->image(
        from_path(FileSystemOp::resourcesPath() / u8"cicilogo.png"));
    logoLabel->setPixmap(img.scaled(22, 22,
                                    Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation));
    logoLabel->setFixedSize(22, 22);
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->setStyleSheet(QStringLiteral(
        "background: transparent; border: none;"));
    titleLayout->addWidget(logoLabel);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    statusLabel_ = new QLabel(
        tr("Complete sign-in in your browser."), this);
    statusLabel_->setStyleSheet(QStringLiteral(
        "font-size: 12px;"
        "color: #aeb5c2;"
        "background: transparent;"
        "border: none;"));
    statusLabel_->setFixedWidth(280);
    statusLabel_->setWordWrap(false);
    statusLabel_->setAlignment(Qt::AlignCenter);

    QPushButton *googleBtn = new QPushButton(this);
    googleBtn->setFixedSize(260, 42);
    googleBtn->setCursor(Qt::PointingHandCursor);
    QIcon icon(app_->assets()->image(from_path(FileSystemOp::resourcesPath() / "g.png")));
    googleBtn->setIcon(icon);
    googleBtn->setIconSize(QSize(18, 18));
    
    if (s != LoginLogic::Logining) {
        googleBtn->setText(tr("Sign in with Google"));
    }
    else {
        googleBtn->setEnabled(false);
        googleBtn->setText(tr("Signing in..."));
    }
        
    // QSS specific to the Google button.
    googleBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #ffffff;"
        "  color: #757575;"
        "  border: 1px solid #dadce0;"
        "  border-radius: 4px;"
        "  font-family: 'Roboto', sans-serif;"
        "  font-size: 14px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background-color: #f8f9fa;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #f1f3f4;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #f1f3f4;"
        "  color: #9aa0a6;"
        "}"
    );

    layout->addLayout(titleLayout);
    layout->addSpacing(8);
    layout->addWidget(statusLabel_, 0, Qt::AlignHCenter);
    layout->addSpacing(22);
    layout->addWidget(googleBtn, 0, Qt::AlignHCenter);
    layout->addStretch();
    
    loginBtn_ = googleBtn;
    QObject::connect(loginBtn_, &QPushButton::clicked, this, &LoginWidget::login);
}

void LoginWidget::showAsModal(QWidget* parent) {
	this->setWindowFlags(Qt::Dialog
                         | Qt::WindowTitleHint
                         | Qt::WindowSystemMenuHint
                         | Qt::WindowCloseButtonHint
                         | Qt::MSWindowsFixedSizeDialogHint);
	this->winId();
    applyCiciWindowIcon(this);
    this->setWindowModality(Qt::ApplicationModal);

    if (parent) {
	    auto w = this->windowHandle();
        w->setTransientParent(parent->windowHandle());
    }

    if (parent) {
        QRect parentRect = parent->geometry();
        int x = parentRect.x() + (parentRect.width() - this->width()) / 2;
        int y = parentRect.y() + (parentRect.height() - this->height()) / 2;
        this->move(x, y);
    }

    this->show();
    this->raise();
    this->activateWindow();
}

void LoginWidget::login() {
    if (app_->loginLogic()->state() == LoginLogic::Logining) {
        return;
    }

    reportLoginUiAction(surface_, LoginUiAction::SignInClick);
    statusLabel_->setText(tr("Waiting for sign-in in your browser..."));
    loginBtn_->setText(tr("Signing in..."));
    loginBtn_->setEnabled(false);
    app_->loginLogic()->login();
}

void LoginWidget::onLoginSuccess() {
    statusLabel_->setText(tr("Complete sign-in in your browser."));
    loginBtn_->setEnabled(true);
    loginBtn_->setText(tr("Sign in with Google"));
    hide();
}

void LoginWidget::onLoginFailed(LoginFailureReason reason, const std::u8string& error) {
    if (reason == LoginFailureReason::ServiceUnavailable) {
        statusLabel_->setText(tr("Login service unavailable."));
    } else if (error == u8"Login timeout") {
        statusLabel_->setText(tr("Sign-in timed out. Please try again."));
    } else if (error == u8"Failed to open browser") {
        statusLabel_->setText(tr("Unable to open your browser. Please try again."));
    } else {
        statusLabel_->setText(tr("Sign-in failed. Please try again."));
    }
    loginBtn_->setEnabled(true);
    loginBtn_->setText(tr("Sign in with Google"));
}

void LoginWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    reportLoginUiAction(surface_, LoginUiAction::Show);
}

void LoginWidget::closeEvent(QCloseEvent* event) {
    if (event->spontaneous()) {
        reportLoginUiAction(surface_, LoginUiAction::Close);
    }
    QWidget::closeEvent(event);
}
