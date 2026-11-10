// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QWidget>

#include "../statistics/loginanalytics.h"
#include "protocol/account.h"

class QCloseEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class GlobalModel;
class LoginWidget: public QWidget {
    Q_OBJECT
    
public:
    LoginWidget(GlobalModel* app,
                LoginUiSurface surface,
                QWidget* parent);
    ~LoginWidget();
    
public:
    void showAsModal(QWidget* parent);
    
private:
    void createUI();
    
private slots:
    void login();
    void onLoginSuccess();
    void onLoginFailed(LoginFailureReason reason, const std::u8string& error);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    
private:
    GlobalModel *app_ = nullptr;
    LoginUiSurface surface_ = LoginUiSurface::Modal;
    QLabel* statusLabel_ = nullptr;
    QPushButton* loginBtn_ = nullptr;
};
