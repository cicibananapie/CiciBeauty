// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <string>

#include <QPointer>

#include "ciciwidget.h"
#include "../modelapp.h"

class QLabel;
class QListWidget;
class QComboBox;
class QPushButton;
class QShowEvent;
class QStackedWidget;
class QWidget;
typedef struct calldata calldata_t;
typedef struct signal_handler signal_handler_t;

class AccountEntryWidget;
class AIEffectController;
class BasicSettingController;
class BeautyEnhancementController;
class BodyShapingSettingController;
class FeedbackDialog;
class FilterSettingController;
class ImportExportController;
class KeyingSettingController;
class LensEffectsController;
class MakeupSettingController;
class SpecialEffectsAssetsController;
class SettingAccessOverlay;
class SubscriptionTipWidget;

struct SettingTarget {
    std::u8string sourceUuid;
    std::u8string filterUuid;

    bool isValid() const {
        return !sourceUuid.empty() && !filterUuid.empty();
    }
};

class SettingController: public CiciWidget, public IEffectConflictResolver {
    Q_OBJECT
    
public:
    SettingController(GlobalModel *app, QWidget *parent = nullptr);
    ~SettingController();
    
public:
    void switchLanguage();
    void createUI();
    SettingTarget activeSettingTarget() const;
    bool isActiveSettingTarget(const SettingTarget& target) const;
    nlohmann::json activeSettingConfig(const SettingTarget& target) const;
    void importActiveSettingConfig(const SettingTarget& target,
                                   nlohmann::json config,
                                   const nlohmann::json& favorites,
                                   std::function<void(bool)> complete);
    void onCiciFilterChanged(const std::u8string& sourceUuid);
    void confirmEffectActivation(QWidget* owner, uint64_t effectId,
                                 std::function<void(bool)> complete) override;
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void uiSelectedSourceChanged(const std::u8string& id);
    void loginRequested();
    
private:
    //bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent* event) override;
    
private:
    void updateAccessOverlay();
    void updateAccessOverlayGeometry(bool allowAccountAccess);
    void showFeedback();
    void resetState(bool resetChildren);
    void refreshSourceCombo();
    void refreshApplyFilterButton();
    bool isSettingTabEnabled(int row) const;
    void updateSettingTabsEnabled(bool hasFilter);
    std::u8string selectedSourceUuid() const;
    std::u8string selectedCiciFilterUuid() const;
    void loadSettingConfig(const std::u8string& sourceUuid);
    void applySettingPatch(const nlohmann::json& patch);
    bool ensurePageCreated(int row);
    void refreshAccountWidgets();
    void reloadFavoriteViews();
    void clearAccountWidgets();
    void replacePage(int row, QWidget* page);
    void syncPageFromConfig(int row, const nlohmann::json& config);
    bool resolveEffectConflicts(const nlohmann::json& disablePatch);
    void syncSelectedFilterModel();
    void connectObsSignals();
    void disconnectObsSignals();
    static void handleObsSourceListSignal(void* data, calldata_t* cd);
    
private slots:
    void onSourceComboCurrentIndexChanged(int index);
    void onApplyFilterClicked();
    void onObsSourceListChanged();
    void onTabCurrentRowChanged(int row);
    void onLoginSuccess();
    void onLoginFailed(const std::u8string& error);
    void onLogouted();
    
private:
    SettingAccessOverlay* accessOverlay_ = nullptr;
    SubscriptionTipWidget* subscriptionTipWidget_ = nullptr;
    AIEffectController* aiEffect_ = nullptr;
    BasicSettingController* basicSetting_ = nullptr;
    FilterSettingController* filterSetting_ = nullptr;
    BeautyEnhancementController* beautyEnhancement_ = nullptr;
    MakeupSettingController* makeupSetting_ = nullptr;
    BodyShapingSettingController* bodyShapingSetting_ = nullptr;
    SpecialEffectsAssetsController* specialEffectsAssets_ = nullptr;
    KeyingSettingController* keyingSetting_ = nullptr;
    LensEffectsController* lensEffectsSetting_ = nullptr;
    ImportExportController* importExport_ = nullptr;
    QWidget* leftPanel_ = nullptr;
    AccountEntryWidget* accountEntry_ = nullptr;
    QPointer<FeedbackDialog> feedbackDialog_;
    QComboBox* sourceCombo_ = nullptr;
    QPushButton* applyFilterBtn_ = nullptr;
    QPushButton* feedbackBtn_ = nullptr;
    //QLabel* qqService_ = nullptr;
    //QLabel* guide_ = nullptr;
    QListWidget* tabBar_ = nullptr;
    QStackedWidget* stackedWidget_ = nullptr;
    signal_handler_t* obsSignalHandler_ = nullptr;
    GlobalModel *app_ = nullptr;
    bool uiCreated_ = false;
};
