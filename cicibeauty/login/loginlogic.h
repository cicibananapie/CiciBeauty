// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QObject>

#include "protocol/account.h"

// Qt interface with a local snapshot. ciciluma owns the login state machine.
class LoginLogic : public QObject, public LoginStates {
    Q_OBJECT
public:
  explicit LoginLogic(QObject* parent);
  ~LoginLogic() override;

  static void openProfilePage();
  void autoLogin();
  void login();
  void logout();

  // Local reads only.
  State state() const {
      return info_.state;
  }
  bool hasUsableSession() const {
      return info_.hasUsableSession();
  }
  const std::u8string& uid() const {
      return info_.uid;
  }
  const std::u8string& email() const {
      return info_.email;
  }
  const std::u8string& nickname() const {
      return info_.nickname;
  }
  const std::u8string& avatarUrl() const {
      return info_.avatarUrl;
  }

signals:
  void loginReady(LoginReadySource source, const std::u8string& uid);
  void interactiveLoginReady();
  void logouted();
  void loginFailed(LoginFailureReason reason, const std::u8string& errorMessage);

private:
  void refresh();

  LoginInfo info_;
};
