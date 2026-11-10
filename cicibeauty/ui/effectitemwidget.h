// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>

#include <string_view>

#include <QBasicTimer>
#include <QMetaObject>
#include <QRect>

#include "imagetextwidget.h"
#include "../effectcatalog.h"

class EffectPackageStatus;
class GlobalModel;
enum class EffectPackageState : uint8_t;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QTimerEvent;

class EffectItemWidget : public ImageTextWidget {
public:
    explicit EffectItemWidget(GlobalModel* app, QWidget* parent = nullptr);
    ~EffectItemWidget() override;

    void setId(uint32_t id) override;
    void bindEffect(const EffectCatalog::EffectInfo& effect);
    EffectPackageStatus* packageStatus() const;
    bool packageReady() const;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

private:
    enum class BindingKind : uint8_t {
        Unbound,
        Bundled,
        Remote,
    };

    void applyPackageState(EffectPackageState state);
    void drawPackageState(QPainter* painter) const;
    QRect packageStateRect(int size) const;
    void startDownloadingAnimation();
    void stopDownloadingAnimation();

private:
    GlobalModel* app_ = nullptr;
    EffectPackageStatus* packageStatus_ = nullptr;
    BindingKind bindingKind_ = BindingKind::Unbound;
    QMetaObject::Connection packageStateConnection_;
    QBasicTimer downloadingTimer_;
    int downloadingAngle_ = 0;
};
