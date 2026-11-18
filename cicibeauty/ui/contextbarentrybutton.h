// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QColor>
#include <QPointer>
#include <QPushButton>

class QEvent;
class QLabel;
class MarqueeLabel;
class QPaintEvent;

class ContextBarEntryButton : public QPushButton {
    Q_OBJECT

public:
    struct StatusColors {
        QColor border;
        QColor text;
        QColor background;

        bool operator==(const StatusColors&) const = default;
    };

    explicit ContextBarEntryButton(QWidget* parent = nullptr);

    void followHeightOf(QWidget* widget);
    void setStatus(const QString& text, const StatusColors& colors);
    QString statusText() const;

    QSize sizeHint() const override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void syncReferenceHeight();
    void updateStatusColor();

private:
    QLabel* iconLabel_ = nullptr;
    QLabel* brandLabel_ = nullptr;
    QWidget* statusContainer_ = nullptr;
    MarqueeLabel* statusLabel_ = nullptr;
    QPointer<QWidget> heightReference_;
    StatusColors statusColors_;
};
