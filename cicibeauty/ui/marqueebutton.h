// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QPushButton>
#include <QRect>
#include <QtGlobal>

class QEvent;
class QHideEvent;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class QStyleOptionButton;
class QTimerEvent;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QEnterEvent;
#endif

class MarqueeButton : public QPushButton {
public:
    explicit MarqueeButton(QWidget* parent = nullptr);
    explicit MarqueeButton(const QString& text, QWidget* parent = nullptr);
    ~MarqueeButton() override;

    void setMarqueeSpeed(int pxPerSecond);
    int marqueeSpeed() const;

    void setMarqueeGap(int px);
    int marqueeGap() const;

    void setMarqueeStartDelay(int ms);
    int marqueeStartDelay() const;

protected:
    void paintEvent(QPaintEvent* event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QRect contentRect(const QStyleOptionButton& opt) const;
    int textWidth(const QStyleOptionButton& opt) const;
    int textFlags(const QStyleOptionButton& opt) const;
    int marqueeOffset(int textWidth) const;
    bool textOverflows(const QStyleOptionButton& opt, QRect* outContentRect = nullptr, int* outTextWidth = nullptr) const;
    bool shouldMarquee(const QStyleOptionButton& opt, QRect* outContentRect = nullptr, int* outTextWidth = nullptr) const;
    void drawElidedText(QPainter& painter, const QStyleOptionButton& opt, const QRect& rect) const;
    void drawMarqueeText(QPainter& painter, const QStyleOptionButton& opt, const QRect& rect, int textWidth) const;
    void updateMarqueeState();
    void stopMarqueeTimer();

private:
    QBasicTimer marqueeTimer_;
    QElapsedTimer marqueeElapsed_;
    bool hovering_ = false;
    int marqueeSpeed_ = 40;
    int marqueeGap_ = 24;
    int marqueeStartDelay_ = 300;
};
