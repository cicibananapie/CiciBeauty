// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QLabel>
#include <QRect>
#include <QtGlobal>

class QEvent;
class QHideEvent;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class QTimerEvent;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QEnterEvent;
#endif

class MarqueeLabel : public QLabel {
public:
    explicit MarqueeLabel(QWidget* parent = nullptr);
    explicit MarqueeLabel(const QString& text, QWidget* parent = nullptr);
    ~MarqueeLabel() override;

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
    bool canDrawSingleLineText() const;
    QRect textRect() const;
    int effectiveIndent() const;
    int textWidth() const;
    int textFlags() const;
    int marqueeOffset(int width) const;
    bool textOverflows(QRect* outTextRect = nullptr, int* outTextWidth = nullptr) const;
    bool shouldMarquee(QRect* outTextRect = nullptr, int* outTextWidth = nullptr) const;
    void drawFrame(QPainter& painter) const;
    void drawElidedText(QPainter& painter, const QRect& rect) const;
    void drawMarqueeText(QPainter& painter, const QRect& rect, int width) const;
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
