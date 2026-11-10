// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPixmap>
#include <QPointer>
#include <QRect>

#include "ciciwidget.h"

class AssetsMgr;
class RemoteImageStatus;
class QHideEvent;
class QTimerEvent;
class ImageTextWidget: public CiciWidget{
    Q_OBJECT
    
public:
    ImageTextWidget(AssetsMgr* assets, QWidget* parent = nullptr);
    ~ImageTextWidget();
    
public:
    void setImage(const QString& path);
    void setImage(const QPixmap& img);
    void setText(const QString& txt);
    QString text() const;
    
    void setSelected(bool b);
    bool isSelected() const {return selected_;}
    void setFavorite(bool b);
    bool isFavorate() const {return isFavorate_;}
    void setHoverEffectEnabled(bool enabled);
    void setFavoriteEnabled(bool enabled);
    void setClickableEnabled(bool enabled);
    bool isClickableEnabled() const { return clickableEnabled_; }
    void activate();
    void setImageDisplaySize(const QSize& size);
    
signals:
    void clicked();
    void liked(bool);
    
public:
    QSize sizeHint() const override;
    
protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    
private:
    void drawHeart(QPainter *painter, const QRectF &rect);
    void drawMarqueeText(QPainter *painter, const QRect& rect, int textWidth) const;
    void drawDisplayText(QPainter *painter, const QRect& rect) const;
    void _setImage(const QString& path);
    void clearRemoteImageBinding();
    void showDefaultImage();
    void updateTextMetrics();
    QRect textRect() const;
    int textWidth() const;
    int marqueeOffset(int textWidth) const;
    bool shouldMarqueeText() const;
    void updateMarqueeState();
    void stopMarqueeTimer();
    void resetMarqueeState();
    
private:
    QPixmap defaultImg_;
    QPixmap image_;
    QSize imageDisplaySize_;
    QString text_;
    QRect textRC_;
    QBasicTimer marqueeTimer_;
    QElapsedTimer marqueeElapsed_;
    bool hovered_ = false;
    bool selected_ = false;
    bool isFavorate_ = false;
    bool hoverEffectEnabled_ = true;
    bool favoriteEnabled_ = true;
    bool clickableEnabled_ = true;
    AssetsMgr* assetsMgr_ = nullptr;
    QPointer<RemoteImageStatus> remoteImageStatus_;
    QMetaObject::Connection remoteImageStateConnection_;
};
