// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once
#include <stdint.h>

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <QObject>
#include <QPointer>
#include <obs-frontend-api.h>

#include "modelapp.h"
#include "../cicibeautyStub/ciciplugin.h"

class AssetsMgr;
class ContextBarEntryController;
class CiciBeautyFilterInstance;
class EffectCatalog;
class EffectPackageStatus;
class LoginLogic;
class LoginWidget;
class ISettingConfig;
class SettingController;
class SourceListCiciStatusController;
class SubscriptionController;
class UpdateClient;
class QWidget;
typedef struct obs_data obs_data_t;
typedef struct obs_properties obs_properties_t;
typedef struct obs_source obs_source_t;
struct obs_key_event;
struct obs_mouse_event;
struct obs_source_frame;

class CiciBeautyApp : public QObject
                     , public GlobalModel
                     , public ICiciBeautyPlugin {
    Q_OBJECT
    friend class CiciBeautyFilterInstance;
    
public:
    CiciBeautyApp();
    ~CiciBeautyApp();

    CiciBeautyApp(const CiciBeautyApp&) = delete;
    CiciBeautyApp& operator=(const CiciBeautyApp&) = delete;

public:

    AssetsMgr* assets() const override;
    UpdateClient* updater() const;
    LoginLogic* loginLogic() const override;
    ISettingConfig* settingConfig() const override;
    const EffectCatalog* effectCatalog() const override;
    SubscriptionController* subscriptionController() const override;

    void showCici();
    void ensureDockCreated();
    bool ensureContextBarEntryAttached();

private slots:
    void onUISelectedSourceChanged(const std::u8string& id);

public:
    static void* onFilterCreate(obs_data_t* settings, obs_source_t* source);
    static const char* onFilterName(void* unused);
    static void onFilterDestroy(void* data);
    static void onActive(void* data);
    static void onDeactive(void* data);
    static void onShow(void* data);
    static void onHide(void* data);
    static obs_source_frame* onFilterVideo(void* data, obs_source_frame* frame);
    static obs_properties_t* onFilterProperties(void* data);
    static void onFilterUpdate(void* data, obs_data_t* settings);
    static void onMouseClick(void* data,
                             const obs_mouse_event* event,
                             int32_t type,
                             bool mouse_up,
                             uint32_t click_count);
    static void onMouseMove(void* data, const obs_mouse_event* event, bool mouse_leave);
    static void onMouseWheel(void* data, const obs_mouse_event* event, int x_delta, int y_delta);
    static void onFocus(void* data, bool focus);
    static void onKeyClick(void* data, const obs_key_event* event, bool key_up);
    static void onFilterRemove(void* data, obs_source_t* source);
    static void onFilterAdd(void* data, obs_source_t* source);

private:
    void startGlobalServices();
    void stopGlobalServices();

    void ensureLoginLogic();
    void ensureSettingConfig();
    void ensureEffectCatalog();
    void ensureAssets();
    void ensureUpdater();

    void createSettingWidget();

    void addToolMenuItem();
    bool ensureSourceListCiciStatusAttached();
    void autoLogin();
    void showLoginWindowIfNeeded();
    void activateObsMainWindow();
    void checkUpdate();
    CiciBeautyFilterInstance *createFilterInstance(obs_source_t *source);
    void destroyFilterInstance(CiciBeautyFilterInstance *instance);
    void destroyFilterInstanceOnUiThread(CiciBeautyFilterInstance *instance);
    bool containsFilterInstance(const CiciBeautyFilterInstance *instance) const;
    CiciBeautyFilterInstance *findFilterInstance(const std::u8string& filterUuid) const;
    void setCurrentInstance(CiciBeautyFilterInstance *instance);
    void notifyFilterModelChanged(FilterModel *oldModel, FilterModel *newModel);
    void scheduleCiciFilterChanged(const std::u8string& sourceUuid);

    static void onFrontendEventCallback(enum obs_frontend_event event, void* private_data);
    void onFrontendEvent(enum obs_frontend_event event);

private:    //ICiciBeautyPlugin
    bool onLoad() override;
    void onUnLoad() override;
    void onLocaleChanged(const char* locale) override;

private:    //GlobalModel
    FilterModel *filterModel() override;
    bool hasCiciFilterOnSource(const std::u8string& sourceUuid) override;
    bool applyCiciFilterToSource(const std::u8string& sourceUuid,
                                 const std::u8string& trigger) override;
    bool removeCiciFilterFromSource(const std::u8string& sourceUuid,
                                    const std::u8string& trigger) override;
    void registerFilterModelChangeObserver(IFilterModelChangeObserver *observer) override;
    void unregisterFilterModelChangeObserver(IFilterModelChangeObserver *observer) override;
    
private:
    QPointer<QWidget> dockContainer_;
    QPointer<SettingController> settingWidget_;
    std::unique_ptr<ContextBarEntryController> contextBarEntry_;
    std::unique_ptr<SourceListCiciStatusController> sourceListCiciStatus_;
    std::unique_ptr<AssetsMgr> assets_;
    std::unique_ptr<UpdateClient> updater_;
    std::unique_ptr<LoginLogic> loginLogic_;
    std::unique_ptr<ISettingConfig> settingConfig_;
    std::unique_ptr<EffectCatalog> effectCatalog_;
    std::unique_ptr<SubscriptionController> subscriptionController_;
    QPointer<LoginWidget> loginWidget_;

    std::vector<std::unique_ptr<CiciBeautyFilterInstance>> filters_;
    std::vector<IFilterModelChangeObserver *> filterModelChangeObservers_;
    CiciBeautyFilterInstance* currentInstance_ = nullptr;

    bool globalStarted_ = false;
    bool toolMenuAdded_ = false;
};
