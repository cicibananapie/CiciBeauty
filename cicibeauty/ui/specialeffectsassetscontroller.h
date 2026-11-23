// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>
#include <vector>
#include <QList>
#include <QString>

#include "ciciwidget.h"
#include "../obsinfo.h"
#include "../modelapp.h"

class CategorizedCardGrid;
class QCheckBox;
class EffectItemWidget;
class GlobalModel;

class SpecialEffectsAssetsController: public CiciWidget, public IObsEventCallback, public IFilterModelChangeObserver {
    Q_OBJECT

public:
    SpecialEffectsAssetsController(GlobalModel* app, QWidget* parent = nullptr);
    ~SpecialEffectsAssetsController();

    void createUI();
    void switchLanguage();
    void reloadFavorites();

    void reset() override;
    void json2Me(const nlohmann::json& j) override;
    nlohmann::json me2Json() const;

signals:
    void settingPatchRequested(const nlohmann::json& patch);

private:
    void onHVModeChanged(ObsInfo::HVMode mode) override;
    void onFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) override;

    struct Category {
        uint32_t id_;
        QString title_;
        QList<EffectItemWidget*> cards_;
        EffectItemWidget* selected_ = nullptr;
    };

    void loadEffectList();
    void refreshCategories();
    QList<QWidget*> favoriteCards() const;
    bool isEffectCompatible(uint32_t effectId) const;
    void selectEffect(uint32_t categoryIndex, EffectItemWidget* card);
    void setSelectedEffect(Category& category, EffectItemWidget* card);
    void setEffectFavorite(EffectItemWidget* card, bool favorite);

private slots:
    void onSoundStateChanged(int state);
    void onCategoryActivated(const QString& view);

private:
    QCheckBox* enableAudio_ = nullptr;
    CategorizedCardGrid* cardGrid_ = nullptr;
    std::vector<Category> categories_;
    GlobalModel* app_ = nullptr;
};
