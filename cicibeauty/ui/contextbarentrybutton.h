// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QPointer>
#include <QPushButton>

class QEvent;
class QLabel;
class MarqueeLabel;
class QPaintEvent;

class ContextBarEntryButton : public QPushButton {
    Q_OBJECT

public:
    enum class Tone {
        Neutral,
        Danger,
        Success,
        Trial,
        Confirming
    };

    explicit ContextBarEntryButton(QWidget* parent = nullptr);

    void followHeightOf(QWidget* widget);
    void setStatus(const QString& text, Tone tone);
    QString statusText() const;
    Tone tone() const;

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
    Tone tone_ = Tone::Neutral;
};
