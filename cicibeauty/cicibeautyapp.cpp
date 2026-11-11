// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "cicibeautyapp.h"
#include "lumaclient.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string_view>
#include <utility>

#include <obs-module.h>
#include <obs.h>
#include <obs-frontend-api.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QApplication>
#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QVBoxLayout>

#if CICI_OS_MACOS
#define EXPORT_API __attribute__((visibility("default")))
#elif CICI_OS_WINDOWS
#include <Windows.h>
#define EXPORT_API __declspec(dllexport)
#else
#error "Unsupported platform"
#endif

#include "cicibeautyfilterinstance.h"
#include "cicibase.h"
#include "ciciconfigure.h"
#include "cicibeautyconfigure.h"
#include "effectcatalog.h"
#include "i18nsupport.h"
#include "log.h"
#include "misc.h"
#include "nlohmann/json.hpp"
#include "obscallbackthreadlogger.h"
#include "qstringconv.h"
#include "ui/commonui.h"
#include "login/loginlogic.h"
#include "settingconfig.h"
#include "statistics/eventid.h"
#include "statistics/cicireport.h"
#include "protocol/session.h"
#include "statistics/settingsanalytics.h"
#include "subscription/subscriptioncontroller.h"
#include "ui/assetsmgr.h"
#include "ui/contextbarentrycontroller.h"
#include "ui/loginwidget.h"
#include "ui/settingcontroller.h"
#include "ui/sourcelistcicistatuscontroller.h"
#include "update/updateclient.h"

namespace {

constexpr const char* kCiciFilterId = "cicbeauty";
constexpr const char* kCiciFilterName = "Cici";
constexpr const char* kDockId = "cicibeauty7d4f92a1-e3b5-4c68-b9f2-8d7a1c0e3a4b";
constexpr char8_t kDuplicateFilterRejected[] = u8"duplicate_filter_rejected";

CiciBeautyApp* g_app = nullptr;
CiciBeautyApp gApp;

struct obs_source_info ciciFilterInfo2 = {
    .id = kCiciFilterId,
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_VIDEO,

    .get_name = CiciBeautyApp::onFilterName,
    .create = CiciBeautyApp::onFilterCreate,
    .destroy = CiciBeautyApp::onFilterDestroy,
    .get_properties = CiciBeautyApp::onFilterProperties,
    .update = CiciBeautyApp::onFilterUpdate,
    .activate = CiciBeautyApp::onActive,
    .deactivate = CiciBeautyApp::onDeactive,
    .show = CiciBeautyApp::onShow,
    .hide = CiciBeautyApp::onHide,
    .filter_video = CiciBeautyApp::onFilterVideo,
    .mouse_click = CiciBeautyApp::onMouseClick,
    .mouse_move = CiciBeautyApp::onMouseMove,
    .mouse_wheel = CiciBeautyApp::onMouseWheel,
    .focus = CiciBeautyApp::onFocus,
    .key_click = CiciBeautyApp::onKeyClick,
    .filter_remove = CiciBeautyApp::onFilterRemove,
    .filter_add = CiciBeautyApp::onFilterAdd,
};

QString ciciBeautyWindowTitle() {
    return QObject::tr("Cici Beauty V%1").arg(from_u8(std::u8string(CICI_VERSION)));
}

std::u8string obsString(const char* value) {
    return value != nullptr ? to_u8(value) : std::u8string();
}

std::u8string obsSourceUuid(obs_source_t* source) {
    return obsString(source != nullptr ? obs_source_get_uuid(source) : nullptr);
}

std::u8string obsSourceType(obs_source_t* source) {
    return obsString(source != nullptr ? obs_source_get_id(source) : nullptr);
}

bool isCiciApplicableSource(obs_source_t* source) {
    if (source == nullptr) {
        return false;
    }
    if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT) {
        return false;
    }

    const uint32_t flags = obs_source_get_output_flags(source);
    return (flags & OBS_SOURCE_VIDEO) != 0;
}

struct CiciFilterContext {
    obs_source_t* filter = nullptr;
};

void enumCiciFilter(obs_source_t*, obs_source_t* filter, void* param) {
    auto* ctx = static_cast<CiciFilterContext*>(param);
    if (ctx == nullptr || ctx->filter != nullptr || filter == nullptr) {
        return;
    }

    const char* id = obs_source_get_id(filter);
    if (id != nullptr && std::strcmp(id, kCiciFilterId) == 0) {
        ctx->filter = obs_source_get_ref(filter);
    }
}

obs_source_t* ciciFilterFromSource(obs_source_t* source) {
    if (source == nullptr) {
        return nullptr;
    }

    CiciFilterContext ctx;
    obs_source_enum_filters(source, enumCiciFilter, &ctx);
    return ctx.filter;
}

bool hasCiciFilter(obs_source_t* source) {
    obs_source_t* filter = ciciFilterFromSource(source);
    if (filter == nullptr) {
        return false;
    }

    obs_source_release(filter);
    return true;
}

struct CiciFilterScanResult {
    std::u8string_view addedFilterUuid;
    obs_source_t* addedFilterRef = nullptr;
    std::u8string otherCiciFilterUuid;
};

void enumCiciFiltersForAdd(obs_source_t*, obs_source_t* filter, void* param) {
    auto* result = static_cast<CiciFilterScanResult*>(param);
    if (result == nullptr || filter == nullptr) {
        return;
    }

    const char* id = obs_source_get_id(filter);
    if (id == nullptr || std::strcmp(id, kCiciFilterId) != 0) {
        return;
    }

    const std::u8string filterUuid = obsSourceUuid(filter);
    if (filterUuid == result->addedFilterUuid) {
        if (result->addedFilterRef == nullptr) {
            // OBS holds the parent's filter mutex during enumeration. Retain the
            // filter here and remove it only after enumeration has completed.
            result->addedFilterRef = obs_source_get_ref(filter);
        }
        return;
    }

    if (result->otherCiciFilterUuid.empty()) {
        result->otherCiciFilterUuid = filterUuid;
    }
}

std::recursive_mutex& ciciFilterAddMutex() {
    // Removing a duplicate emits OBS signals synchronously. A signal subscriber
    // can add another filter on the same thread before the removal returns.
    static std::recursive_mutex mutex;
    return mutex;
}

std::u8string uniqueCiciFilterName(obs_source_t* source) {
    std::u8string base = to_u8(kCiciFilterName);
    std::u8string name = base;
    int suffix = 2;

    while (true) {
        obs_source_t* existing = obs_source_get_filter_by_name(source, to_char(name));
        if (existing == nullptr) {
            return name;
        }

        obs_source_release(existing);
        name = base + u8" " + to_u8(suffix++);
    }
}

std::u8string normalizedFilterTrigger(const std::u8string& trigger) {
    return trigger.empty() ? std::u8string(u8"unknown") : trigger;
}

nlohmann::json filterEventProperties(const std::u8string& trigger,
                                     const char* result,
                                     const char* failureReason,
                                     obs_source_t* source,
                                     std::u8string_view filterUuid = {},
                                     const std::u8string& sourceUuidFallback = {}) {
    const std::u8string sourceUuid = source != nullptr ? obsSourceUuid(source) : sourceUuidFallback;
    nlohmann::json props = nlohmann::json::object({
        {"trigger", to_char(normalizedFilterTrigger(trigger))},
        {"result", result != nullptr ? result : "unknown"},
    });

    if (failureReason != nullptr && failureReason[0] != '\0') {
        props["failure_reason"] = failureReason;
    }
    if (!sourceUuid.empty()) {
        props["source_uuid"] = to_char(sourceUuid);
    }
    if (source != nullptr) {
        const std::u8string sourceType = obsSourceType(source);
        if (!sourceType.empty()) {
            props["source_type"] = to_char(sourceType);
        }
    }
    if (!filterUuid.empty()) {
        props["filter_uuid"] = to_json(std::u8string(filterUuid));
    }

    return props;
}
}  // namespace

CiciBeautyApp::CiciBeautyApp() : QObject(nullptr) {
	initCiciLogConfig(ciciDefaultLogLevel(), u8"ciciOBS.log");
	initCiciBase();
    g_app = this;

	QObject::connect(qApp, &QApplication::aboutToQuit, [this]() { 
        stopGlobalServices();
	});
}

CiciBeautyApp::~CiciBeautyApp() {
    stopGlobalServices();
    if (g_app == this) {
        g_app = nullptr;
    }
}

CiciBeautyFilterInstance* CiciBeautyApp::createFilterInstance(obs_source_t* source) {
    auto instance = std::make_unique<CiciBeautyFilterInstance>(this, source);
    CiciBeautyFilterInstance* raw = instance.get();
    filters_.push_back(std::move(instance));

    LogInfo << "CiciBeautyApp2::createFilterInstance count=" << filters_.size() << logEnd;
    return raw;
}

void CiciBeautyApp::destroyFilterInstance(CiciBeautyFilterInstance* instance) {
	assert(instance != nullptr);
    if (currentInstance_ == instance) {
        setCurrentInstance(nullptr);
    }

    const auto oldSize = filters_.size();
    filters_.erase(std::remove_if(filters_.begin(),
                                  filters_.end(),
                                  [instance](const std::unique_ptr<CiciBeautyFilterInstance>& item) {
                                      return item.get() == instance;
                                  }),
                   filters_.end());

    LogInfo << "CiciBeautyApp2::destroyFilterInstance count=" << filters_.size()
            << " oldCount=" << oldSize << logEnd;
}

bool CiciBeautyApp::containsFilterInstance(const CiciBeautyFilterInstance* instance) const {
    return std::any_of(filters_.begin(),
                       filters_.end(),
                       [instance](const std::unique_ptr<CiciBeautyFilterInstance>& item) {
                           return item.get() == instance;
                       });
}

CiciBeautyFilterInstance* CiciBeautyApp::findFilterInstance(
    const std::u8string& filterUuid) const {
    if (filterUuid.empty()) {
        return nullptr;
    }

    for (const auto& item : filters_) {
        if (item->filterUuid() == filterUuid) {
            return item.get();
        }
    }

    return nullptr;
}

void CiciBeautyApp::destroyFilterInstanceOnUiThread(CiciBeautyFilterInstance* instance) {
    assert(instance != nullptr);
    QCoreApplication* qtApp = QCoreApplication::instance();
    assert(qtApp != nullptr);

    if (QThread::currentThread() == qtApp->thread()) {
	    instance->detachObsBindings();
        destroyFilterInstance(instance);
        return;
    }

    instance->detachObsBindings();

    QPointer<CiciBeautyApp> self(this);
    QMetaObject::invokeMethod(
	    qtApp,
	    [self, instance]() {
            if (self == nullptr) {
                return;
            }
            if (!self->containsFilterInstance(instance)) {
                return;
            }
            self->destroyFilterInstance(instance);
        }, Qt::QueuedConnection);
}

void CiciBeautyApp::setCurrentInstance(CiciBeautyFilterInstance* instance) {
    if (currentInstance_ == instance) {
        return;
    }

    FilterModel* oldModel = currentInstance_;
    currentInstance_ = instance;
    LogInfo << "CiciBeautyApp2::setCurrentInstance instance=" << currentInstance_ << logEnd;
    notifyFilterModelChanged(oldModel, currentInstance_);
}

void CiciBeautyApp::notifyFilterModelChanged(FilterModel* oldModel, FilterModel* newModel) {
    auto observers = filterModelChangeObservers_;
	for (auto *observer : observers) {
		assert(observer != nullptr);
        observer->onFilterModelChanged(oldModel, newModel);
    }
}

void CiciBeautyApp::scheduleCiciFilterChanged(const std::u8string& sourceUuid) {
    QPointer<CiciBeautyApp> self(this);
    auto notify = [self, sourceUuid]() {
        if (self == nullptr) {
            return;
        }
        if (self->settingWidget_ != nullptr) {
            self->settingWidget_->onCiciFilterChanged(sourceUuid);
        }
        if (self->sourceListCiciStatus_ != nullptr) {
            self->sourceListCiciStatus_->scheduleRefresh();
        }
    };

    if (QThread::currentThread() == thread()) {
        LogDebug << "Cici filter UI notification dispatch=direct source_uuid="
                 << sourceUuid << logEnd;
        notify();
        return;
    }

    LogDebug << "Cici filter UI notification dispatch=queued source_uuid="
             << sourceUuid << logEnd;
    QMetaObject::invokeMethod(this, std::move(notify), Qt::QueuedConnection);
}

AssetsMgr* CiciBeautyApp::assets() const {
    return assets_.get();
}

UpdateClient* CiciBeautyApp::updater() const {
    return updater_.get();
}

LoginLogic* CiciBeautyApp::loginLogic() const {
    return loginLogic_.get();
}

ISettingConfig* CiciBeautyApp::settingConfig() const {
    assert(settingConfig_ != nullptr);
    return settingConfig_.get();
}

const EffectCatalog* CiciBeautyApp::effectCatalog() const {
    return effectCatalog_.get();
}

SubscriptionController* CiciBeautyApp::subscriptionController() const {
    return subscriptionController_.get();
}

bool CiciBeautyApp::hasCiciFilterOnSource(const std::u8string& sourceUuid) {
    if (sourceUuid.empty()) {
        return false;
    }

    obs_source_t* source = obs_get_source_by_uuid(to_char(sourceUuid));
    if (source == nullptr) {
        return false;
    }

    const bool result = isCiciApplicableSource(source) && hasCiciFilter(source);
    obs_source_release(source);
    return result;
}

bool CiciBeautyApp::applyCiciFilterToSource(const std::u8string& sourceUuid,
                                            const std::u8string& trigger) {
    if (sourceUuid.empty()) {
        CiciReport::reportEvent(KEVT_APP_FILTER_APPLY, filterEventProperties(
            trigger, "failed", "source_uuid_empty", nullptr));
        return false;
    }

    obs_source_t* source = obs_get_source_by_uuid(to_char(sourceUuid));
    if (source == nullptr) {
        CiciReport::reportEvent(KEVT_APP_FILTER_APPLY, filterEventProperties(
            trigger, "failed", "source_not_found", nullptr, {}, sourceUuid));
        return false;
    }

    if (!isCiciApplicableSource(source)) {
        CiciReport::reportEvent(KEVT_APP_FILTER_APPLY, filterEventProperties(
            trigger, "failed", "source_not_applicable", source, {}, sourceUuid));
        obs_source_release(source);
        return false;
    }

    if (hasCiciFilter(source)) {
        CiciReport::reportEvent(KEVT_APP_FILTER_APPLY, filterEventProperties(
            trigger, "failed", "already_has_filter", source, {}, sourceUuid));
        obs_source_release(source);
        return false;
    }

    const std::u8string name = uniqueCiciFilterName(source);
    obs_source_t* filter = obs_source_create(kCiciFilterId, to_char(name), nullptr, nullptr);
    if (filter == nullptr) {
        LogError << "applyCiciFilterToSource failed to create filter" << logEnd;
        CiciReport::reportEvent(KEVT_APP_FILTER_APPLY, filterEventProperties(
            trigger, "failed", "create_filter_failed", source, {}, sourceUuid));
        obs_source_release(source);
        return false;
    }

    CiciBeautyFilterInstance* instance = findFilterInstance(obsSourceUuid(filter));
    assert(instance != nullptr);
    if (instance != nullptr) {
        instance->setPendingApplyTrigger(trigger);
    }
    obs_source_filter_add(source, filter);

    obs_source_release(filter);
    obs_source_release(source);
    return true;
}

bool CiciBeautyApp::removeCiciFilterFromSource(const std::u8string& sourceUuid,
                                               const std::u8string& trigger) {
    if (sourceUuid.empty()) {
        CiciReport::reportEvent(KEVT_APP_FILTER_REMOVE, filterEventProperties(
            trigger, "failed", "source_uuid_empty", nullptr));
        return false;
    }

    obs_source_t* source = obs_get_source_by_uuid(to_char(sourceUuid));
    if (source == nullptr) {
        CiciReport::reportEvent(KEVT_APP_FILTER_REMOVE, filterEventProperties(
            trigger, "failed", "source_not_found", nullptr, {}, sourceUuid));
        return false;
    }

    obs_source_t* filter = ciciFilterFromSource(source);
    if (filter == nullptr) {
        CiciReport::reportEvent(KEVT_APP_FILTER_REMOVE, filterEventProperties(
            trigger, "failed", "filter_not_found", source, {}, sourceUuid));
        obs_source_release(source);
        return false;
    }

    CiciBeautyFilterInstance* instance = findFilterInstance(obsSourceUuid(filter));
    assert(instance != nullptr);
    if (instance != nullptr) {
        instance->setPendingRemoveTrigger(trigger);
    }
    obs_source_filter_remove(source, filter);

    obs_source_release(filter);
    obs_source_release(source);
    return true;
}

void CiciBeautyApp::showCici() {
    QMainWindow* mainWin = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
    if (mainWin == nullptr) {
        LogWarn << "CiciBeautyApp2::showCici: OBS main window is null" << logEnd;
        return;
    }

    QDockWidget* dock = mainWin->findChild<QDockWidget*>(kDockId);
    if (dock == nullptr) {
        LogWarn << "CiciBeautyApp2::showCici: dock is not ready" << logEnd;
        return;
    }

    subscriptionController()->refresh();

    dock->show();
    dock->raise();
    if (dock->isFloating()) {
        dock->activateWindow();
    }
}

void CiciBeautyApp::ensureDockCreated() {
    if (dockContainer_ != nullptr) {
        return;
    }

    QMainWindow* mainWin = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
    if (mainWin == nullptr) {
        LogWarn << "CiciBeautyApp2::ensureDockCreated: OBS main window is null" << logEnd;
        return;
    }

    const QString title = ciciBeautyWindowTitle();
    dockContainer_ = new QWidget;
    auto* layout = new QVBoxLayout(dockContainer_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const QByteArray titleUtf8 = title.toUtf8();
    if (!obs_frontend_add_dock_by_id(
            kDockId, titleUtf8.constData(), dockContainer_.data())) {
        LogError << "CiciBeautyApp2::ensureDockCreated: failed to register dock"
                 << logEnd;
        delete dockContainer_;
        dockContainer_ = nullptr;
        return;
    }

    createSettingWidget();
    if (auto* dock = mainWin->findChild<QDockWidget*>(kDockId);
        dock != nullptr) {
        applyCiciWindowIcon(dock);
        QObject::connect(dock->toggleViewAction(),
                         &QAction::triggered,
                         this,
                         [this](bool visible) {
                             if (visible) {
                                 reportSettingsOpen(SettingsOpenEntry::DocksMenu);
                                 showCici();
                             }
                         });
    }
}

bool CiciBeautyApp::ensureContextBarEntryAttached() {
    if (contextBarEntry_ == nullptr) {
        contextBarEntry_ = std::make_unique<ContextBarEntryController>(
            subscriptionController(),
            [this]() {
                reportSettingsOpen(SettingsOpenEntry::ContextBar);
                showCici();
            });
    }

    return contextBarEntry_->attach();
}

bool CiciBeautyApp::ensureSourceListCiciStatusAttached() {
    if (sourceListCiciStatus_ == nullptr) {
        sourceListCiciStatus_ =
            std::make_unique<SourceListCiciStatusController>(this);
    }

    return sourceListCiciStatus_->attach();
}

void CiciBeautyApp::onUISelectedSourceChanged(const std::u8string &id) {
    CiciBeautyFilterInstance *filter = nullptr;
    if (id.empty()) {
        LogInfo << "[SourceSelection] resolve uuid=<empty>, instance=0, controller=0"
                << logEnd;
        setCurrentInstance(nullptr);
        return;
    }

    for (const auto& item : filters_) {
        if (item->sourceUuid() == id) {
            filter = item.get();
            break;
        }
    }

    obs_source_t* parent = filter != nullptr
        ? obs_get_source_by_uuid(to_char(id))
        : nullptr;
    LogInfo << "[SourceSelection] resolve uuid=" << id
            << ", sourceName="
            << (parent != nullptr && obs_source_get_name(parent) != nullptr
                    ? obs_source_get_name(parent)
                    : "")
            << ", instance=" << filter
            << ", filterCount=" << filters_.size() << logEnd;
    if (parent != nullptr) {
        obs_source_release(parent);
    }
    setCurrentInstance(filter);
}

void* CiciBeautyApp::onFilterCreate(obs_data_t* settings, obs_source_t* source) {
    logObsCallbackThread("CiciBeautyApp2::onFilterCreate");
    (void)settings;
    return g_app->createFilterInstance(source);
}

const char* CiciBeautyApp::onFilterName(void* unused) {
    logObsCallbackThread("CiciBeautyApp2::onFilterName");
    (void)unused;
    return "Cici Beauty";
}

void CiciBeautyApp::onFilterDestroy(void* data) {
    logObsCallbackThread("CiciBeautyApp2::onFilterDestroy");
    auto* instance = reinterpret_cast<CiciBeautyFilterInstance*>(data);

    CiciBeautyApp* app = instance->app();
    app->destroyFilterInstanceOnUiThread(instance);
}

void CiciBeautyApp::onActive(void* data) {
    logObsCallbackThread("CiciBeautyApp2::onActive");
    (void)data;
}

void CiciBeautyApp::onDeactive(void *data) {
    logObsCallbackThread("CiciBeautyApp2::onDeactive");
    (void)data;
}

void CiciBeautyApp::onShow(void *data) {
    logObsCallbackThread("CiciBeautyApp2::onShow");
    (void)data;
}

void CiciBeautyApp::onHide(void *data) {
    logObsCallbackThread("CiciBeautyApp2::onHide");
    (void)data;
}

obs_source_frame* CiciBeautyApp::onFilterVideo(void* data, obs_source_frame* frame) {
    logObsCallbackThread("CiciBeautyApp2::onFilterVideo");
    auto* instance = reinterpret_cast<CiciBeautyFilterInstance*>(data);
    return instance->onFilterVideo(frame);
}

obs_properties_t* CiciBeautyApp::onFilterProperties(void* data) {
    logObsCallbackThread("CiciBeautyApp2::onFilterProperties");
    (void)data;
    LogInfo << "CiciBeautyApp2::onFilterProperties" << logEnd;
    return nullptr;
}

void CiciBeautyApp::onFilterUpdate(void *data, obs_data_t *settings) {
    logObsCallbackThread("CiciBeautyApp2::onFilterUpdate");
    (void)data;
    (void)settings;
}

void CiciBeautyApp::onMouseClick(void* data,
                                  const obs_mouse_event* event,
                                  int32_t type,
                                  bool mouse_up,
				  uint32_t click_count) {
    logObsCallbackThread("CiciBeautyApp2::onMouseClick");
    (void)data;
    (void)event;
    (void)type;
    (void)mouse_up;
    (void)click_count;
}

void CiciBeautyApp::onMouseMove(void *data, const obs_mouse_event *event, bool mouse_leave) {
    logObsCallbackThread("CiciBeautyApp2::onMouseMove");
    (void)data;
    (void)event;
    (void)mouse_leave;
}

void CiciBeautyApp::onMouseWheel(void *data, const obs_mouse_event *event, int x_delta, int y_delta) {
    logObsCallbackThread("CiciBeautyApp2::onMouseWheel");
    (void)data;
    (void)event;
    (void)x_delta;
    (void)y_delta;
}

void CiciBeautyApp::onFocus(void *data, bool focus) {
    logObsCallbackThread("CiciBeautyApp2::onFocus");
    (void)data;
    (void)focus;
}

void CiciBeautyApp::onKeyClick(void *data, const obs_key_event *event, bool key_up) {
    logObsCallbackThread("CiciBeautyApp2::onKeyClick");
    (void)data;
    (void)event;
    (void)key_up;
}

void CiciBeautyApp::onFilterRemove(void *data, obs_source_t *source) {
    logObsCallbackThread("CiciBeautyApp2::onFilterRemove");
	auto *instance = reinterpret_cast<CiciBeautyFilterInstance *>(data);
    const std::u8string sourceUuid = obsSourceUuid(source);
    const std::u8string trigger = instance->consumePendingRemoveTrigger(u8"obs_remove");
    LogInfo << "Cici filter removed source_uuid=" << sourceUuid
            << " filter_uuid=" << instance->filterUuid()
            << " trigger=" << trigger << logEnd;
    if (trigger != kDuplicateFilterRejected) {
        CiciReport::reportEvent(KEVT_APP_FILTER_REMOVE, filterEventProperties(
            trigger, "success", nullptr, source, instance->filterUuid()));
    }
    instance->onFilterRemove();
    instance->app()->scheduleCiciFilterChanged(sourceUuid);
}

void CiciBeautyApp::onFilterAdd(void *data, obs_source_t *source) {
    logObsCallbackThread("CiciBeautyApp2::onFilterAdd");
	auto *instance = reinterpret_cast<CiciBeautyFilterInstance *>(data);
    const std::u8string sourceUuid = obsSourceUuid(source);
    const std::u8string addedFilterUuid = instance->filterUuid();

    CiciFilterScanResult scan{addedFilterUuid};
    {
        std::lock_guard<std::recursive_mutex> guard(ciciFilterAddMutex());
        obs_source_enum_filters(source, enumCiciFiltersForAdd, &scan);

        if (scan.addedFilterRef != nullptr && !scan.otherCiciFilterUuid.empty()) {
            LogWarn << "Rejecting duplicate Cici filter source_uuid=" << sourceUuid
                    << " existing_filter_uuid=" << scan.otherCiciFilterUuid
                    << " rejected_filter_uuid=" << addedFilterUuid << logEnd;
            instance->setPendingRemoveTrigger(kDuplicateFilterRejected);
            obs_source_filter_remove(source, scan.addedFilterRef);
        }
    }

    assert(scan.addedFilterRef != nullptr);
    if (scan.addedFilterRef == nullptr) {
        LogError << "Cici filter add invariant failed: added filter is not attached"
                << " source_uuid=" << sourceUuid
                << " filter_uuid=" << addedFilterUuid << logEnd;
        return;
    }

    if (!scan.otherCiciFilterUuid.empty()) {
        const std::u8string trigger =
            instance->consumePendingApplyTrigger(u8"obs_scene_restore");
        obs_source_release(scan.addedFilterRef);

        auto properties = filterEventProperties(
            trigger, "failed", "already_has_filter", source, addedFilterUuid);
        properties["existing_filter_uuid"] = to_char(scan.otherCiciFilterUuid);
        CiciReport::reportEvent(KEVT_APP_FILTER_APPLY, properties);
        return;
    }

    instance->onFilterAdd(source);

    const std::u8string trigger = instance->consumePendingApplyTrigger(u8"obs_scene_restore");
    LogInfo << "Cici filter accepted source_uuid=" << sourceUuid
            << " filter_uuid=" << addedFilterUuid
            << " trigger=" << trigger << logEnd;
    CiciReport::reportEvent(KEVT_APP_FILTER_APPLY, filterEventProperties(
        trigger, "success", nullptr, source, addedFilterUuid));
    instance->app()->scheduleCiciFilterChanged(sourceUuid);
    obs_source_release(scan.addedFilterRef);
}

void CiciBeautyApp::startGlobalServices() {
    if (globalStarted_) {
        return;
    }

    const auto startedAtMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    const char* obsVersion = obs_get_version_string();
    if (LumaClient::instance().start()) {
        LumaClient::instance().postMsg(cici::protocol::initializeSession("obs", obsVersion != nullptr ? obsVersion : "",
                                                                         ProcessThreadOp::pid(), startedAtMs,
                                                                         to_char(std::u8string(CICI_VERSION)),
                                                                         isOfficialBuild(),
                                                                         to_char(std::u8string(CICI_CHANNEL_ID))));
    }
    CiciReport::reportEvent(KEVT_APP_LAUNCH, nlohmann::json::object({
        {"launch_source", "obs_plugin"},
        {"official_build", isOfficialBuild()},
    }));

    subscriptionController_ = std::make_unique<SubscriptionController>();
    ensureLoginLogic();
    ensureSettingConfig();
    autoLogin();
    ensureAssets();
    ensureUpdater();

    ensureEffectCatalog();
    ensureDockCreated();

    obs_frontend_add_event_callback(onFrontendEventCallback, this);

    globalStarted_ = true;
}

void CiciBeautyApp::stopGlobalServices() {
    if (!globalStarted_) {
        return;
    }

    const auto shutdownStartedAt = std::chrono::steady_clock::now();
    auto runShutdownPhase = [&shutdownStartedAt](const char* phase, auto&& action) {
        const auto phaseStartedAt = std::chrono::steady_clock::now();
        LogInfo << "[ShutdownTrace] begin phase=" << phase << logEnd;
        action();
        const auto now = std::chrono::steady_clock::now();
        const auto phaseElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - phaseStartedAt).count();
        const auto totalElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - shutdownStartedAt).count();
        LogInfo << "[ShutdownTrace] end phase=" << phase
                << " phase_ms=" << phaseElapsedMs
                << " total_ms=" << totalElapsedMs << logEnd;
    };

    LogInfo << "[ShutdownTrace] begin" << logEnd;

    runShutdownPhase("frontend_callback", [this]() {
        obs_frontend_remove_event_callback(onFrontendEventCallback, this);
    });
    runShutdownPhase("setting_widget", [this]() {
        if (settingWidget_ != nullptr) {
            delete settingWidget_;
            settingWidget_ = nullptr;
        }
        delete dockContainer_;
        dockContainer_ = nullptr;
    });
    runShutdownPhase("login_widget", [this]() {
        if (loginWidget_ != nullptr) {
            loginWidget_->close();
            loginWidget_->deleteLater();
            loginWidget_ = nullptr;
        }
    });
    runShutdownPhase("obs_ui", [this]() {
        contextBarEntry_.reset();
        sourceListCiciStatus_.reset();
        setCurrentInstance(nullptr);
    });
    runShutdownPhase("filters", [this]() {
        filters_.clear();
    });
    runShutdownPhase("updater", [this]() {
        updater_.reset();
    });
    runShutdownPhase("subscription", [this]() {
        subscriptionController_.reset();
    });
    runShutdownPhase("login", [this]() {
        loginLogic_.reset();
    });
    runShutdownPhase("effect_catalog", [this]() {
        effectCatalog_.reset();
    });
    runShutdownPhase("setting_config", [this]() {
        settingConfig_.reset();
    });
    runShutdownPhase("assets", [this]() {
        assets_.reset();
    });
    runShutdownPhase("exit_event", []() {
        CiciReport::reportEvent(KEVT_APP_EXIT, nlohmann::json::object({
            {"exit_reason", "global_services_stop"},
        }));
    });
    runShutdownPhase("luma_stop", []() {
        LumaClient::instance().stop();
    });

    globalStarted_ = false;
    toolMenuAdded_ = false;

    const auto totalElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - shutdownStartedAt).count();
    LogInfo << "[ShutdownTrace] complete total_ms=" << totalElapsedMs << logEnd;
}


void CiciBeautyApp::ensureSettingConfig() {
    assert(settingConfig_ == nullptr);
    if (settingConfig_ != nullptr) {
        return;
    }

    settingConfig_ = std::make_unique<SettingConfig>();
}

void CiciBeautyApp::ensureEffectCatalog() {
    assert(effectCatalog_ == nullptr);
    if (effectCatalog_ != nullptr) {
        return;
    }

    effectCatalog_ = std::make_unique<EffectCatalog>();
    effectCatalog_->ready = [this]() {
        createSettingWidget();
    };
    effectCatalog_->load();
}

void CiciBeautyApp::ensureLoginLogic() {
    assert(loginLogic_ == nullptr);
    if (loginLogic_ != nullptr) {
        return;
    }
    loginLogic_ = std::make_unique<LoginLogic>(nullptr);
    QObject::connect(loginLogic_.get(), &LoginLogic::interactiveLoginReady, this,
                     &CiciBeautyApp::activateObsMainWindow);
}

void CiciBeautyApp::ensureAssets() {
    assert(assets_ == nullptr);
    if (assets_ == nullptr) {
        assets_ = std::make_unique<AssetsMgr>();
    }
}

void CiciBeautyApp::ensureUpdater() {
    assert(updater_ == nullptr);
    if (updater_ == nullptr) {
        updater_ = std::make_unique<UpdateClient>(this);
    }
}

void CiciBeautyApp::createSettingWidget() {
    if (settingWidget_ != nullptr || dockContainer_ == nullptr || effectCatalog_ == nullptr ||
        !effectCatalog_->isReady()) {
        return;
    }
    settingWidget_ = new SettingController(this);
    QObject::connect(settingWidget_,
                     &SettingController::uiSelectedSourceChanged,
                     this,
                     &CiciBeautyApp::onUISelectedSourceChanged,
                     Qt::UniqueConnection);
    QObject::connect(settingWidget_,
                     &SettingController::loginRequested,
                     this,
                     &CiciBeautyApp::showLoginWindowIfNeeded,
                     Qt::UniqueConnection);
    settingWidget_->createUI();
    dockContainer_->layout()->addWidget(settingWidget_);
}

void CiciBeautyApp::addToolMenuItem() {
    if (toolMenuAdded_) {
        return;
    }

    std::u8string menuTitleU8 = to_u8(ciciBeautyWindowTitle());
    auto* action = static_cast<QAction*>(
        obs_frontend_add_tools_menu_qaction(to_char(menuTitleU8)));
    if (action == nullptr) {
        LogError << "CiciBeautyApp2::addToolMenuItem: failed to create QAction" << logEnd;
        return;
    }

    action->setIcon(ciciPluginIcon());
    QObject::connect(action, &QAction::triggered, this, [this]() {
        reportSettingsOpen(SettingsOpenEntry::ToolsMenu);
        showCici();
    });
    toolMenuAdded_ = true;
}

void CiciBeautyApp::autoLogin() {
    assert(loginLogic_ != nullptr);
    loginLogic_->autoLogin();
}

void CiciBeautyApp::showLoginWindowIfNeeded() {
    assert(loginLogic_ != nullptr);
    if (loginLogic_->hasUsableSession()) {
        return;
    }

    auto* mainWin = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
    if (mainWin == nullptr) {
        LogWarn << "CiciBeautyApp2::showLoginWindowIfNeeded: OBS main window is null" << logEnd;
        return;
    }

    if (loginWidget_ == nullptr) {
        loginWidget_ = new LoginWidget(this, LoginUiSurface::Modal, nullptr);
    }
    loginWidget_->showAsModal(mainWin);
}

void CiciBeautyApp::activateObsMainWindow() {
    auto* mainWin = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
    if (mainWin == nullptr) {
        LogWarn << "CiciBeautyApp2::activateObsMainWindow: OBS main window is null" << logEnd;
        return;
    }

    mainWin->show();
    mainWin->raise();
    mainWin->activateWindow();

#if CICI_OS_WINDOWS
    HWND hwnd = reinterpret_cast<HWND>(mainWin->winId());
    if (hwnd != nullptr) {
        if (IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        }
        SetForegroundWindow(hwnd);
    }
#endif
}

void CiciBeautyApp::checkUpdate() {
    assert(updater_ != nullptr);
    updater_->check();
}

void CiciBeautyApp::onFrontendEventCallback(enum obs_frontend_event event, void* private_data) {
    logObsCallbackThread("CiciBeautyApp2::onFrontendEventCallback");
    auto* app = reinterpret_cast<CiciBeautyApp*>(private_data);
    app->onFrontendEvent(event);
}

void CiciBeautyApp::onFrontendEvent(enum obs_frontend_event event) {
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        addToolMenuItem();
        ensureContextBarEntryAttached();
        ensureSourceListCiciStatusAttached();
        showLoginWindowIfNeeded();
        checkUpdate();
    } else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
        for (const auto& instance : filters_) {
            instance->obsInfo()->refreshSceneBinding();
        }
    } else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP ||
               event == OBS_FRONTEND_EVENT_EXIT) {
        for (const auto& instance : filters_) {
            instance->obsInfo()->disconnectSceneBinding();
        }
    }
}

bool CiciBeautyApp::onLoad() {
    obs_register_source(&ciciFilterInfo2);
    startGlobalServices();
    LogInfo << "CiciBeautyApp2 loaded" << logEnd;
    return true;
}

void CiciBeautyApp::onUnLoad() {
    LogInfo << "CiciBeautyApp2 unloaded" << logEnd;
    stopGlobalServices();
}

void CiciBeautyApp::onLocaleChanged(const char* locale) {
    I18nSupport::setCurrentLocaleName(QString::fromUtf8(locale != nullptr ? locale : ""));
    LogInfo << "CiciBeautyApp2 locale changed:" << I18nSupport::currentLocaleName() << logEnd;
}

FilterModel *CiciBeautyApp::filterModel() {
	return currentInstance_;
}

void CiciBeautyApp::registerFilterModelChangeObserver(IFilterModelChangeObserver* observer) {
	assert(observer != nullptr);
    if (std::find(filterModelChangeObservers_.begin(),
                  filterModelChangeObservers_.end(),
                  observer) == filterModelChangeObservers_.end()) {
        filterModelChangeObservers_.push_back(observer);
    }
}

void CiciBeautyApp::unregisterFilterModelChangeObserver(IFilterModelChangeObserver *observer) {
	assert(observer != nullptr);
    if (auto it = std::find(filterModelChangeObservers_.begin(),
                            filterModelChangeObservers_.end(),
                            observer);
        it != filterModelChangeObservers_.end()) {
        filterModelChangeObservers_.erase(it);
    }
}

extern "C" EXPORT_API ICiciBeautyPlugin *ciciPlugin() {
	return g_app;
}
