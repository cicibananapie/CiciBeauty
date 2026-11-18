// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

class TipsWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TipsWidget(QWidget* parent = nullptr);
    
public:
    void showTip(const QString& text, const QString& actionText = {});

signals:
    void actionTriggered();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateMessage();

private:
    QLabel* messageLabel_ = nullptr;
    QPushButton* actionButton_ = nullptr;
    QString message_;
};
