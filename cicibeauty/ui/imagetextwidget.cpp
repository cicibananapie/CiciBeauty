// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>
#include <filesystem>

#include <QStyle>
#include <QStyleOption>
#include <QAccessible>
#include <QAccessibleWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEvent>
#include <QTimerEvent>
#include <QFontMetrics>
#include <QTextLayout>

#include "misc.h"
#include "imagetextwidget.h"
#include "assetsmgr.h"
#include "../qstringconv.h"
#include "misc.h"

const uint32_t kDefaultMargin = 4;
const uint32_t kDefaultRadius = 3;
const QRect kHeartRC(5, 5, 20, 20);
constexpr int kMarqueeTimerIntervalMs = 16;
constexpr int kMarqueeSpeed = 40;
constexpr int kMarqueeGap = 24;
constexpr int kMarqueeStartDelayMs = 300;

namespace {

class AccessibleImageTextWidget final : public QAccessibleWidget {
public:
    explicit AccessibleImageTextWidget(ImageTextWidget* widget)
        : QAccessibleWidget(widget, QAccessible::Button) {
    }

    QString text(QAccessible::Text type) const override {
        if (type == QAccessible::Name) {
            const QString name = imageTextWidget()->accessibleName();
            return name.isEmpty() ? imageTextWidget()->text() : name;
        }
        return QAccessibleWidget::text(type);
    }

    QAccessible::State state() const override {
        QAccessible::State value = QAccessibleWidget::state();
        if (!imageTextWidget()->isClickableEnabled()) {
            value.disabled = true;
        }
        return value;
    }

    QStringList actionNames() const override {
        if (!imageTextWidget()->isEnabled()
            || !imageTextWidget()->isClickableEnabled()) {
            return {};
        }
        return {QAccessibleActionInterface::pressAction()};
    }

    void doAction(const QString& actionName) override {
        if (actionName == QAccessibleActionInterface::pressAction()) {
            imageTextWidget()->activate();
        }
    }

    QStringList keyBindingsForAction(const QString&) const override {
        return {};
    }

private:
    ImageTextWidget* imageTextWidget() const {
        return static_cast<ImageTextWidget*>(widget());
    }
};

QAccessibleInterface* imageTextWidgetAccessibilityFactory(
    const QString&, QObject* object) {
    auto* widget = qobject_cast<ImageTextWidget*>(object);
    return widget == nullptr ? nullptr : new AccessibleImageTextWidget(widget);
}

void installImageTextWidgetAccessibilityFactory() {
    static const bool installed = []() {
        QAccessible::installFactory(imageTextWidgetAccessibilityFactory);
        return true;
    }();
    (void)installed;
}

} // namespace

ImageTextWidget::ImageTextWidget(AssetsMgr* assets, QWidget* parent, CardStyle cardStyle)
: CiciWidget(parent), cardStyle_(cardStyle), assetsMgr_(assets) {
    installImageTextWidgetAccessibilityFactory();
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QString path = from_path(FileSystemOp::resourcesPath() / "cicilogo.png");
    this->defaultImg_ = assetsMgr_->image(path);
}

ImageTextWidget::~ImageTextWidget() {
    
}

void ImageTextWidget::_setImage(const QString& path) {
    image_ = assetsMgr_->image(path);
    updateGeometry();
    update();
}

void ImageTextWidget::setImage(const QString& path) {
	clearRemoteImageBinding();
	auto pathU8 = to_u8(path);
	if (UrlOp::isUrl(pathU8)) {
        showDefaultImage();
        auto* status = assetsMgr_->requestRemoteImage(path);
        remoteImageStatus_ = status;
        remoteImageStateConnection_ = QObject::connect(
            status,
            &RemoteImageStatus::stateChanged,
            this,
            [this, status](RemoteImageState state) {
                if (remoteImageStatus_ != status || state != RemoteImageState::Ready) {
                    return;
                }
                _setImage(status->localPath());
            });

        if (status->state() == RemoteImageState::Ready) {
            _setImage(status->localPath());
        }
    }
    else {
        QString absolutePath(path);
        std::filesystem::path file(pathU8);
        if (file.is_relative()) {
            absolutePath = from_path(FileSystemOp::resourcesPath() / file);
        }
        else {
            assert(file.is_absolute());
        }
        
        _setImage(absolutePath);
    }
}

void ImageTextWidget::setImage(const QPixmap& img) {
    clearRemoteImageBinding();
    image_ = img;
    updateGeometry();
    update();
}

void ImageTextWidget::setText(const QString& txt) {
    text_ = txt;
    if (cardStyle_ == CardStyle::Wide) {
        setToolTip(text_);
    }
    updateTextMetrics();
    resetMarqueeState();
    updateGeometry();
    update();
}

QString ImageTextWidget::text() const {
    return text_;
}

void ImageTextWidget::setImageDisplaySize(const QSize& size) {
    assert(size.isValid());
    if (imageDisplaySize_ == size) {
        return;
    }

    imageDisplaySize_ = size;
    updateGeometry();
    update();
}

void ImageTextWidget::setSelected(bool b) {
    if (selected_ == b) {
        return;
    }
    
    selected_ = b;
    // Update the style through the property selector.
    setProperty("selected", selected_);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void ImageTextWidget::setFavorite(bool b) {
    if (isFavorate_ == b) {
        return;
    }
    
    isFavorate_ = b;
    update();
}

void ImageTextWidget::setHoverEffectEnabled(bool enabled) {
    if (hoverEffectEnabled_ == enabled) {
        return;
    }

    hoverEffectEnabled_ = enabled;
    if (hoverEffectEnabled_) {
        updateMarqueeState();
    } else {
        resetMarqueeState();
    }
    update();
}

void ImageTextWidget::setFavoriteEnabled(bool enabled) {
    if (favoriteEnabled_ == enabled) {
        return;
    }

    favoriteEnabled_ = enabled;
    if (underMouse()) {
        setCursor((isEnabled() && (clickableEnabled_ || favoriteEnabled_)) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    update();
}

void ImageTextWidget::setClickableEnabled(bool enabled) {
    if (clickableEnabled_ == enabled) {
        return;
    }

    clickableEnabled_ = enabled;
    if (underMouse()) {
        setCursor((isEnabled() && (clickableEnabled_ || favoriteEnabled_)) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
}

void ImageTextWidget::activate() {
    if (isEnabled() && clickableEnabled_) {
        emit clicked();
    }
}

void ImageTextWidget::changeEvent(QEvent *event) {
    if (event->type() == QEvent::EnabledChange) {
        if (isEnabled()) {
            updateMarqueeState();
        } else {
            hovered_ = false;
            setCursor(Qt::ArrowCursor);
            resetMarqueeState();
        }
        update();
    } else if (event->type() == QEvent::FontChange) {
        updateTextMetrics();
        resetMarqueeState();
        updateGeometry();
        update();
    }

    CiciWidget::changeEvent(event);
}

void ImageTextWidget::enterEvent(QEnterEvent *event) {
    if (!isEnabled()) {
        hovered_ = false;
        setCursor(Qt::ArrowCursor);
        CiciWidget::enterEvent(event);
        return;
    }

    hovered_ = true;
    setCursor((clickableEnabled_ || favoriteEnabled_) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    updateMarqueeState();
    if (hoverEffectEnabled_ || favoriteEnabled_) {
        update();
    }
    CiciWidget::enterEvent(event);
}

void ImageTextWidget::leaveEvent(QEvent *event) {
    hovered_ = false;
    setCursor(Qt::ArrowCursor);        // Restore the cursor when leaving.
    resetMarqueeState();
    if (hoverEffectEnabled_ || favoriteEnabled_) {
        update();
    }
    CiciWidget::leaveEvent(event);
}

void ImageTextWidget::hideEvent(QHideEvent *event) {
    hovered_ = false;
    resetMarqueeState();
    CiciWidget::hideEvent(event);
}

void ImageTextWidget::mousePressEvent(QMouseEvent *event) {
    if (isEnabled() && event->button() == Qt::LeftButton) {
        QPoint p = event->pos();
        if (favoriteEnabled_ && favoriteRect().contains(p)) {
            isFavorate_ = !isFavorate_;
            update();
            emit liked(isFavorate_);
        }
        else if (clickableEnabled_) {
            activate();
        }
    }
    CiciWidget::mousePressEvent(event);
}

void ImageTextWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    if (cardStyle_ == CardStyle::Wide) {
        drawWideCard(painter);
    } else {
        drawCompactCard(painter);
        CiciWidget::paintEvent(event);
    }
}

void ImageTextWidget::drawCompactCard(QPainter& painter) {
    // Ensure the stylesheet is applied.
    QStyleOption opt;
    opt.initFrom(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool enabled = isEnabled();
    
    QPixmap* img = &image_;
    if (image_.isNull()) {
        img = &defaultImg_;
    }
    // 1. Define the drawing area.
    int margin = kDefaultMargin;
    QRect imgRect(margin, margin, width() - 2 * margin, height() - textRC_.height() - 2*margin);
    int radius = kDefaultRadius;

    // 2. Draw the rounded background with a shadow (simple version: draw a rounded rectangle directly).
    // Note: real shadows are usually better handled with outer layout margins or a separate QGraphicsDropShadowEffect.
    const bool enableHoverState = isEnabled() && hovered_;
    const bool showHoverEffect = enableHoverState && hoverEffectEnabled_;
    const bool showFavorite = enableHoverState && favoriteEnabled_;
    if (showHoverEffect) {
        painter.setPen(QPen(QColor("#3498db"), 2)); // Add a border on hover.
    } else {
        painter.setPen(Qt::NoPen);
    }
    painter.setBrush(enabled ? QColor(Qt::white) : palette().color(QPalette::Disabled, QPalette::Window));
    painter.drawRoundedRect(rect().adjusted(2,2,-2,-2-textRC_.height()), radius, radius);

    // 3. Draw the image with rounded clipping.
    QPainterPath path;
    path.addRoundedRect(imgRect, radius, radius);
    painter.save();
    painter.setClipPath(path);
    if (!enabled) {
        painter.setOpacity(0.35);
    }
    painter.drawPixmap(imgRect, img->scaled(imgRect.size(), Qt::KeepAspectRatioByExpanding));
    painter.restore();
    
    // 4. Draw the hover overlay.
    if (showHoverEffect) {
        painter.save();
        painter.fillPath(path, QColor(0, 0, 0, 80)); // 30% transparent black.
        painter.restore();
    }
    if (showFavorite) {
        painter.save();
        drawHeart(&painter, kHeartRC);
        painter.restore();
    }

    // 5. Draw text.
    drawDisplayText(&painter, textRect());

    // 6. Draw the selected icon in the top-right corner.
    if (selected_) {
        painter.save();
        if (!enabled) {
            painter.setOpacity(0.45);
        }
        QRect iconRect(width() - 25, 5, 20, 20);
        painter.setBrush(QColor("#2ecc71")); // Green.
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(iconRect);
        painter.setPen(Qt::white);
        painter.drawText(iconRect, Qt::AlignCenter, "✔");
        painter.restore();
    }
    
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);
}

void ImageTextWidget::resizeEvent(QResizeEvent *event) {
    CiciWidget::resizeEvent(event);
    resetMarqueeState();
    updateMarqueeState();
}

void ImageTextWidget::timerEvent(QTimerEvent *event) {
    if (event->timerId() != marqueeTimer_.timerId()) {
        CiciWidget::timerEvent(event);
        return;
    }

    if (!shouldMarqueeText()) {
        stopMarqueeTimer();
        update();
        return;
    }

    update();
}

void ImageTextWidget::updateTextMetrics() {
    QFontMetrics metrics(this->font());
    textRC_ = metrics.boundingRect(text_);
}

QRect ImageTextWidget::textRect() const {
    return QRect(0, height() - textRC_.height(), width(), textRC_.height());
}

int ImageTextWidget::textWidth() const {
    QFontMetrics metrics(this->font());
    return metrics.horizontalAdvance(text_);
}

int ImageTextWidget::marqueeOffset(int width) const {
    if (!marqueeElapsed_.isValid()) {
        return 0;
    }

    const qint64 elapsed = marqueeElapsed_.elapsed() - kMarqueeStartDelayMs;
    if (elapsed <= 0) {
        return 0;
    }

    const int cycleWidth = width + kMarqueeGap;
    if (cycleWidth <= 0) {
        return 0;
    }

    return static_cast<int>((elapsed * kMarqueeSpeed / 1000) % cycleWidth);
}

bool ImageTextWidget::shouldMarqueeText() const {
    const QRect rect = textRect();
    return cardStyle_ == CardStyle::Compact && isEnabled()
           && hovered_
           && hoverEffectEnabled_
           && !text_.isEmpty()
           && rect.width() > 0
           && textWidth() > rect.width();
}

void ImageTextWidget::drawMarqueeText(QPainter *painter, const QRect& rect, int width) const {
    const int offset = marqueeOffset(width);
    const int cycleWidth = width + kMarqueeGap;
    const int flags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine;

    QRect first(rect.left() - offset, rect.top(), width, rect.height());
    painter->drawText(first, flags, text_);

    QRect second(first.left() + cycleWidth, rect.top(), width, rect.height());
    painter->drawText(second, flags, text_);
}

void ImageTextWidget::drawDisplayText(QPainter *painter, const QRect& rect) const {
    if (text_.isEmpty() || rect.width() <= 0 || rect.height() <= 0) {
        return;
    }

    const QFontMetrics metrics(this->font());
    const int width = textWidth();

    painter->save();
    painter->setClipRect(rect);
    painter->setPen(isEnabled() ? QColor("#fff8e1") : palette().color(QPalette::Disabled, QPalette::Text));
    if (shouldMarqueeText()) {
        drawMarqueeText(painter, rect, width);
    } else {
        const QString text = metrics.elidedText(text_, Qt::ElideRight, rect.width());
        painter->drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, text);
    }
    painter->restore();
}

void ImageTextWidget::updateMarqueeState() {
    if (!shouldMarqueeText()) {
        stopMarqueeTimer();
        return;
    }

    if (!marqueeElapsed_.isValid()) {
        marqueeElapsed_.restart();
    }
    if (!marqueeTimer_.isActive()) {
        marqueeTimer_.start(kMarqueeTimerIntervalMs, this);
    }
}

void ImageTextWidget::stopMarqueeTimer() {
    if (marqueeTimer_.isActive()) {
        marqueeTimer_.stop();
    }
    marqueeElapsed_.invalidate();
}

void ImageTextWidget::resetMarqueeState() {
    stopMarqueeTimer();
    update();
}

void ImageTextWidget::clearRemoteImageBinding() {
    QObject::disconnect(remoteImageStateConnection_);
    remoteImageStateConnection_ = {};
    remoteImageStatus_ = nullptr;
}

void ImageTextWidget::showDefaultImage() {
    image_ = defaultImg_;
    updateGeometry();
    update();
}

QSize ImageTextWidget::sizeHint() const {
    if (cardStyle_ == CardStyle::Wide) {
        return QSize(160, 140);
    }

    QSize imgSZ = imageDisplaySize_;
    if (!imgSZ.isValid()) {
        const QPixmap* img = &image_;
        if (image_.isNull()) {
            img = &defaultImg_;
        }
        imgSZ = img->size();
    }

    QSize sz;
    sz.setWidth(imgSZ.width() + 8);
    sz.setHeight(imgSZ.height() + textRC_.height() + 8);
    return sz;
}

QRect ImageTextWidget::favoriteRect() const {
    return cardStyle_ == CardStyle::Wide ? QRect(width() - 37, 8, 28, 28) : kHeartRC;
}

void ImageTextWidget::drawWideCard(QPainter& painter) {
    constexpr int radius = 6;
    constexpr int padding = 10;
    const QRect frame = rect().adjusted(0, 0, -1, -1);
    const int labelHeight = fontMetrics().lineSpacing() * 2 + 12;
    const QRect preview(1, 1, width() - 2, height() - labelHeight - 1);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setPen(Qt::NoPen);
    painter.setBrush(selected_ && isEnabled() ? palette().color(QPalette::Highlight).darker(185)
                                              : palette().color(QPalette::Button));
    painter.drawRoundedRect(frame, radius, radius);

    const QPixmap& image = image_.isNull() ? defaultImg_ : image_;
    if (!preview.isEmpty() && !image.isNull()) {
        QSizeF sourceSize(preview.size());
        sourceSize.scale(QSizeF(image.size()), Qt::KeepAspectRatio);
        const QRectF source((image.width() - sourceSize.width()) / 2,
                            (image.height() - sourceSize.height()) / 3,
                            sourceSize.width(), sourceSize.height());
        painter.save();
        QPainterPath clip;
        clip.addRoundedRect(QRectF(frame).adjusted(1, 1, -1, -1), radius - 1, radius - 1);
        painter.setClipPath(clip);
        if (!isEnabled()) {
            painter.setOpacity(0.45);
        }
        painter.drawPixmap(QRectF(preview), image, source);
        painter.restore();
    }
    if (favoriteEnabled_) {
        drawHeart(&painter, favoriteRect());
    }

    const QRect nameRect = rect().adjusted(padding, preview.bottom() + 1, selected_ ? -30 : -padding, -1);
    painter.setPen(palette().color(QPalette::ButtonText));
    if (nameRect.width() > 0) {
        QTextLayout textLayout(text_, font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        textLayout.setTextOption(option);
        textLayout.beginLayout();
        auto firstLine = textLayout.createLine();
        if (firstLine.isValid()) {
            firstLine.setLineWidth(nameRect.width());
        }
        textLayout.endLayout();

        if (firstLine.isValid()) {
            const auto remaining = text_.mid(firstLine.textLength()).trimmed();
            const auto origin = QPointF(nameRect.x(), nameRect.center().y() - firstLine.height() * (remaining.isEmpty() ? 0.5 : 1.0));
            firstLine.draw(&painter, origin);
            if (!remaining.isEmpty()) {
                const auto lastLine = fontMetrics().elidedText(remaining, Qt::ElideRight, nameRect.width());
                painter.drawText(origin + QPointF(0, firstLine.height() + firstLine.ascent()), lastLine);
            }
        }
    }

    painter.setBrush(Qt::NoBrush);
    const QColor border = selected_ ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid);
    painter.setPen(border);
    painter.drawRoundedRect(frame, radius, radius);
    if (selected_) {
        const QPoint center(width() - 15, nameRect.center().y());
        painter.setPen(QPen(border, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPoint check[] = {center + QPoint(-5, 0), center + QPoint(-1, 4), center + QPoint(6, -4)};
        painter.drawPolyline(check, 3);
    }
}

void ImageTextWidget::drawHeart(QPainter *painter, const QRectF &rect) {
    painter->save();

    const bool favorite = isFavorate_;
    QRectF badgeRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 68));
    painter->drawEllipse(badgeRect);

    QRectF heartRect = rect.adjusted(3.8, 4.2, -3.8, -3.4);
    auto point = [&heartRect](qreal x, qreal y) {
        return QPointF(heartRect.left() + heartRect.width() * x / 24.0,
                       heartRect.top() + heartRect.height() * y / 24.0);
    };

    QPainterPath path;
    path.moveTo(point(12.0, 21.35));
    path.lineTo(point(10.55, 20.03));
    path.cubicTo(point(5.4, 15.36), point(2.0, 12.28), point(2.0, 8.5));
    path.cubicTo(point(2.0, 5.42), point(4.42, 3.0), point(7.5, 3.0));
    path.cubicTo(point(9.24, 3.0), point(10.91, 3.81), point(12.0, 5.08));
    path.cubicTo(point(13.09, 3.81), point(14.76, 3.0), point(16.5, 3.0));
    path.cubicTo(point(19.58, 3.0), point(22.0, 5.42), point(22.0, 8.5));
    path.cubicTo(point(22.0, 12.28), point(18.6, 15.36), point(13.45, 20.04));
    path.lineTo(point(12.0, 21.35));
    path.closeSubpath();

    painter->setPen(QPen(QColor(0, 0, 0, favorite ? 50 : 75), favorite ? 1.8 : 2.4,
                         Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);

    if (favorite) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#ff4d67"));
    } else {
        painter->setPen(QPen(QColor(255, 255, 255), 1.6,
                             Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
    }
    
    painter->drawPath(path);
    painter->restore();
}
