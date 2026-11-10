// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QWidget>

class AvatarImageWidget;
class GlobalModel;
class QLabel;
class MarqueeLabel;
class QEvent;
class QEnterEvent;
class QMouseEvent;
class QPropertyAnimation;

class AccountEntryWidget : public QWidget {
    Q_OBJECT

public:
    explicit AccountEntryWidget(GlobalModel* app, QWidget* parent = nullptr);

    void setAccount(const QString& avatarUrl, const QString& displayName);
    void clearAccount();
    int maximumInteractiveHeight() const;

signals:
    void loginRequested();

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void setExpanded(bool expanded, bool animated);
    void updateSubscriptionStatus();
    void updateStyle();

private:
    AvatarImageWidget* avatar_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    MarqueeLabel* subscriptionLabel_ = nullptr;
    QWidget* actionArea_ = nullptr;
    QPropertyAnimation* expansionAnimation_ = nullptr;
    GlobalModel* app_ = nullptr;
    QString displayName_;
    bool hovered_ = false;
    bool expanded_ = false;
};
