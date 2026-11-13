// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <vector>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <set>

#include <obs.h>

#include <QComboBox>
#include <QEvent>
#include <QMouseEvent>
#include <QListWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QTabWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QFile>
#include <QTextStream>
#include <QPushButton>
#include <QRegion>
#include <QSignalBlocker>
#include <QMetaObject>
#include <QMessageBox>

#include "cicibase.h"
#include "nlohmann/json.hpp"
#include "accountentrywidget.h"
#include "commonui.h"
#include "settingaccessoverlay.h"
#include "settingcontroller.h"
#include "subscriptionui.h"

#include "hptimer.h"
#include "log.h"
#include "misc.h"
#include "basicsettingcontroller.h"
#include "bodyshapingsettingcontroller.h"
#include "filtersettingcontroller.h"
#include "importexportcontroller.h"
#include "keyingsettingcontroller.h"
#include "lenseffectscontroller.h"
#include "makeupsettingcontroller.h"
#include "aieffectcontroller.h"
#include "specialeffectsassetscontroller.h"
#include "beautyenhancementcontroller.h"
#include "../login/loginlogic.h"
#include "../feedback/feedbackdialog.h"
#include "../modelapp.h"
#include "../obscallbackthreadlogger.h"
#include "../settingconfig.h"
#include "../effectcatalog.h"
#include "../qstringconv.h"
#include "jsonutility.h"
#include "../statistics/settingsanalytics.h"
#include "../subscription/subscriptioncontroller.h"

namespace {

constexpr const char* kCiciFilterId = "cicbeauty";
constexpr int kNavigationWidth = 200;
constexpr int kHeaderLeftMargin = 4;
constexpr int kSettingPageMinWidth = 640;
constexpr int kSettingPageMinHeight = 520;

struct SourceItem {
    std::u8string uuid;
    std::u8string name;
};

enum PageIndex {
    PageBasic = 0,
    PageFilter,
    PageBeauty,
    PageMakeup,
    PageBodyShaping,
    PageAIEffect,
    PageEffectAsset,
    PageKeying,
    PageLens,
    PageImportExport,
    PageCount
};

void reportSettingsTabClick(int row) {
    switch (row) {
        case PageBasic:
            reportSettingsTabSelect(SettingsTab::Basic);
            break;
        case PageFilter:
            reportSettingsTabSelect(SettingsTab::Filter);
            break;
        case PageBeauty:
            reportSettingsTabSelect(SettingsTab::BeautyEnhancement);
            break;
        case PageMakeup:
            reportSettingsTabSelect(SettingsTab::Makeup);
            break;
        case PageBodyShaping:
            reportSettingsTabSelect(SettingsTab::BodyShaping);
            break;
        case PageAIEffect:
            reportSettingsTabSelect(SettingsTab::AiEffect);
            break;
        case PageEffectAsset:
            reportSettingsTabSelect(SettingsTab::EffectAsset);
            break;
        case PageKeying:
            reportSettingsTabSelect(SettingsTab::Keying);
            break;
        case PageLens:
            reportSettingsTabSelect(SettingsTab::LensEffect);
            break;
        case PageImportExport:
            reportSettingsTabSelect(SettingsTab::ImportExport);
            break;
        default:
            assert(false);
            break;
    }
}

int settingPageFromPath(std::string_view path) {
    constexpr std::string_view sections[] = {
        "/basic", "/filters", "/beauty", "/makeup", "/bodyShaping",
        "/aiEffects", "/spEffects", "/keying", "/lensEffects",
    };
    const auto section = path.substr(0, path.find('/', 1));
    for (int page = PageBasic; page <= PageLens; ++page) {
        if (section == sections[page]) {
            return page;
        }
    }
    return -1;
}

std::u8string obsString(const char* value) {
    if (value == nullptr) {
        return {};
    }

    return to_u8(value);
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

bool enumSourceItems(void* data, obs_source_t* source) {
    auto* items = static_cast<std::vector<SourceItem>*>(data);
    if (items == nullptr) {
        return true;
    }
    if (!isCiciApplicableSource(source)) {
        return true;
    }

    SourceItem item;
    item.uuid = obsString(obs_source_get_uuid(source));
    item.name = obsString(obs_source_get_name(source));
    if (item.uuid.empty()) {
        return true;
    }
    if (item.name.empty()) {
        item.name = item.uuid;
    }

    items->push_back(std::move(item));
    return true;
}

std::vector<SourceItem> collectSourceItems() {
    std::vector<SourceItem> items;
    obs_enum_sources(enumSourceItems, &items);
    return items;
}

QString displayNameFromIdentity(const std::u8string& nickname,
                                const std::u8string& email,
                                const std::u8string& uid) {
    QString nicknameText = from_u8(nickname);
    if (!nicknameText.trimmed().isEmpty()) {
        return nicknameText.trimmed();
    }

    QString emailText = from_u8(email);
    auto at = emailText.indexOf('@');
    if (at > 0) {
        return emailText.left(at);
    }
    if (!emailText.trimmed().isEmpty()) {
        return emailText.trimmed();
    }

    QString uidText = from_u8(uid);
    if (!uidText.trimmed().isEmpty()) {
        return uidText.trimmed();
    }

    return {};
}

struct CiciFilterContext {
    obs_source_t* filter = nullptr;
};

void enumCiciFilter(obs_source_t*, obs_source_t* filter, void* param) {
    auto* ctx = static_cast<CiciFilterContext*>(param);
    if (ctx->filter != nullptr || filter == nullptr) {
        return;
    }

    const char* id = obs_source_get_id(filter);
    if (id != nullptr && std::strcmp(id, kCiciFilterId) == 0) {
        ctx->filter = obs_source_get_ref(filter);
    }
}

obs_source_t* ciciFilterFromSource(obs_source_t* source) {
    CiciFilterContext ctx;
    obs_source_enum_filters(source, enumCiciFilter, &ctx);
    return ctx.filter;
}

} // namespace

SettingController::SettingController(GlobalModel *app, QWidget *parent) : CiciWidget(parent), app_(app) {
}

void SettingController::createUI() {
    sourceCombo_ = createComboBox();
    sourceCombo_->setObjectName(QStringLiteral("ciciSourceCombo"));
    sourceCombo_->setFixedWidth(kNavigationWidth - kHeaderLeftMargin);

    applyFilterBtn_ = createMarqueeButton(tr("Apply Cici"));
    applyFilterBtn_->setObjectName(QStringLiteral("ciciApplyFilterButton"));
    
    const QString headerLinkStyle = QStringLiteral("color: #58C7FF;");
    /*qqService_ = createLabel(tr("QQ Service"));
    qqService_->setStyleSheet(headerLinkStyle);
    
    guide_ = createLabel(tr("Guide"));
    guide_->setStyleSheet(headerLinkStyle);*/

    feedbackBtn_ = createPushButton(tr("Feedback"));
    feedbackBtn_->setCursor(Qt::PointingHandCursor);
    feedbackBtn_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  color: #58C7FF;"
        "  background-color: transparent;"
        "  border: 1px solid #3E7A99;"
        "  border-radius: 3px;"
        "  padding: 0px 12px;"
        "}"
        "QPushButton:hover {"
        "  border-color: #58C7FF;"
        "  background-color: rgba(88, 199, 255, 30);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(88, 199, 255, 55);"
        "}"));
    feedbackBtn_->setMinimumSize(feedbackBtn_->sizeHint());
    
    QHBoxLayout* hbox = new QHBoxLayout();
    hbox->setAlignment(Qt::AlignVCenter);
    hbox->addWidget(sourceCombo_);
    hbox->addWidget(applyFilterBtn_);
    hbox->addStretch();
    //hbox->addWidget(qqService_);
    //hbox->addWidget(guide_);
    hbox->addWidget(feedbackBtn_);
    hbox->setSpacing(10);
    hbox->setContentsMargins(kHeaderLeftMargin, 10, 10, 0);
    
    QFrame* horizontalLine = new QFrame;
    horizontalLine->setFrameShape(QFrame::HLine);
    horizontalLine->setLineWidth(0);
    horizontalLine->setFrameShadow(QFrame::Plain);
    horizontalLine->setContentsMargins(0, 0, 0, 0);
    
    HPTimer tt;
    tt.start();
    tabBar_ = new QListWidget;
    tabBar_->setContentsMargins(0, 0, 0, 0);
    tabBar_->setFixedWidth(kNavigationWidth);
    tabBar_->setStyleSheet("border-radius: 0px;");

    accountEntry_ = new AccountEntryWidget(app_);
    connect(accountEntry_, &AccountEntryWidget::loginRequested,
            this, &SettingController::loginRequested);

    leftPanel_ = new QWidget;
    leftPanel_->setFixedWidth(kNavigationWidth);
    auto* leftLayout = new QVBoxLayout(leftPanel_);
    leftLayout->setContentsMargins(0, 0, 0, 4);
    leftLayout->setSpacing(4);
    leftLayout->addWidget(tabBar_, 1);
    leftLayout->addWidget(accountEntry_);

    stackedWidget_ = new QStackedWidget;
    stackedWidget_->setContentsMargins(0, 0, 0, 0);
    stackedWidget_->setMinimumSize(kSettingPageMinWidth, kSettingPageMinHeight);
    LogTrace << "UI State 1:" << tt.lastElapsedUS() <<logEnd;
    auto addLazyPage = [this](const QString& title) {
        tabBar_->addItem(title);
        auto* placeholder = new QWidget;
        placeholder->setMinimumSize(kSettingPageMinWidth, kSettingPageMinHeight);
        stackedWidget_->addWidget(placeholder);
    };
    addLazyPage(tr("Basic Setting"));
    addLazyPage(tr("Filter Setting"));
    addLazyPage(tr("Beauty Enhancement"));
    addLazyPage(tr("Makeup Setting"));
    addLazyPage(tr("Body Shaping"));
    addLazyPage(tr("AI Effect (Coming soon)"));
    addLazyPage(tr("Effect Asset"));
    addLazyPage(tr("Keying Setting"));
    addLazyPage(tr("Lens Effect"));
    addLazyPage(tr("Im&Export"));
    
    connect(tabBar_, &QListWidget::currentRowChanged, this, &SettingController::onTabCurrentRowChanged);
    connect(tabBar_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
                reportSettingsTabClick(tabBar_->row(item));
            });
    QObject::connect(sourceCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
                     this, &SettingController::onSourceComboCurrentIndexChanged);
    QObject::connect(sourceCombo_, qOverload<int>(&QComboBox::activated),
                     this, [this](int index) {
                         const std::u8string sourceUuid = selectedSourceUuid();
                         obs_source_t* source = obs_get_source_by_uuid(to_char(sourceUuid));
                         std::u8string sourceType;
                         std::u8string sourceName;
                         if (source != nullptr) {
                             sourceType = obsString(obs_source_get_id(source));
                             sourceName = obsString(obs_source_get_name(source));
                             obs_source_release(source);
                         }
                         LogInfo << "[SourceSelection] activated index=" << index
                                 << ", name=" << sourceCombo_->currentText()
                                 << ", uuid=" << sourceUuid << logEnd;
                         reportSettingsSourceSelect(sourceUuid, sourceType, sourceName);
                     });
    QObject::connect(applyFilterBtn_, &QPushButton::clicked,
                     this, &SettingController::onApplyFilterClicked);
    QObject::connect(feedbackBtn_, &QPushButton::clicked,
                     this, [this]() {
                         reportSupportUiAction(SupportUiAction::FeedbackClick);
                         showFeedback();
                     });
    
    subscriptionTipWidget_ = new SubscriptionTipWidget(app_);

    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);
    rightLayout->addWidget(subscriptionTipWidget_);
    rightLayout->addWidget(stackedWidget_, 1);

    QHBoxLayout* hbox2 = new QHBoxLayout;
    hbox2->addWidget(leftPanel_);
    hbox2->addWidget(rightPanel);
    hbox2->setContentsMargins(0, 0, 0, 0);
    
    
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(hbox);
    //vbox->addWidget(horizontalLine);
    vbox->addItem(hbox2);
    vbox->setContentsMargins(0, 0, 0, 0);
    this->setLayout(vbox);
    
    //qqService_->installEventFilter(this);
    //guide_->installEventFilter(this);
    
    accessOverlay_ = new SettingAccessOverlay(app_, this);
    accessOverlay_->setGeometry(this->rect());
    accessOverlay_->hide();

    QObject::connect(app_->loginLogic(), &LoginLogic::loginReady, this, [this](LoginReadySource, const std::u8string&) {
        onLoginSuccess();
    });
    QObject::connect(app_->loginLogic(), &LoginLogic::logouted, this, &SettingController::onLogouted);
    QObject::connect(app_->loginLogic(), &LoginLogic::loginFailed, this,
                     [this](LoginFailureReason, const std::u8string& error) {
        onLoginFailed(error);
    });
    QObject::connect(app_->subscriptionController(),
                     &SubscriptionController::subscriptionInfoUpdated,
                     this, &SettingController::updateAccessOverlay);
    connectObsSignals();
    refreshSourceCombo();
    syncSelectedFilterModel();
    loadSettingConfig(selectedSourceUuid());
    refreshAccountWidgets();
    updateAccessOverlay();
    uiCreated_ = true;
}

void SettingController::updateAccessOverlay() {
    if (!app_->loginLogic()->hasUsableSession()) {
        updateAccessOverlayGeometry(true);
        accessOverlay_->showLoginMask();
        accessOverlay_->show();
        accessOverlay_->raise();
        return;
    }

    const SubscriptionType type = app_->subscriptionController()->subscriptionType();
    if (type == NotSubscribed || type == Expired) {
        updateAccessOverlayGeometry(true);
        accessOverlay_->showSubscription();
        accessOverlay_->show();
        accessOverlay_->raise();
        return;
    }

    accessOverlay_->clearMask();
    accessOverlay_->hide();
}

void SettingController::updateAccessOverlayGeometry(bool allowAccountAccess) {
    accessOverlay_->setGeometry(rect());
    if (!allowAccountAccess || accountEntry_ == nullptr) {
        accessOverlay_->clearMask();
        return;
    }

    const QPoint accountBottomLeft =
        accountEntry_->mapTo(this, QPoint(0, accountEntry_->height()))
        - accessOverlay_->pos();
    const int accountAccessHeight = app_->loginLogic()->hasUsableSession()
        ? accountEntry_->maximumInteractiveHeight()
        : accountEntry_->height();
    const QRect accountAccessRect(
        accountBottomLeft.x(),
        accountBottomLeft.y() - accountAccessHeight,
        accountEntry_->width(),
        accountAccessHeight);

    QRegion overlayRegion(accessOverlay_->rect());
    overlayRegion -= QRegion(accountAccessRect.intersected(accessOverlay_->rect()));
    accessOverlay_->setMask(overlayRegion);
}

void SettingController::showFeedback() {
    if (feedbackDialog_ != nullptr) {
        feedbackDialog_->show();
        feedbackDialog_->raise();
        feedbackDialog_->activateWindow();
        return;
    }

    const QString email = app_->loginLogic() != nullptr
        ? from_u8(app_->loginLogic()->email())
        : QString();
    feedbackDialog_ = new FeedbackDialog(email, this);
    feedbackDialog_->setAttribute(Qt::WA_DeleteOnClose);
    feedbackDialog_->show();
}

SettingController::~SettingController() {
    disconnectObsSignals();
}

void SettingController::confirmEffectActivation(QWidget* owner, uint64_t effectId,
                                               std::function<void(bool)> complete) {
    assert(owner != nullptr && effectId != 0);
    const SettingTarget target = activeSettingTarget();
    auto* model = app_->filterModel();
    if (!target.isValid() || model == nullptr || model->filterUuid() != target.filterUuid ||
        !isEnabled()) {
        complete(false);
        return;
    }

    setEnabled(false);
    model->canActivateEffect(
        this, effectId,
        [this, owner = QPointer<QWidget>(owner), target, complete = std::move(complete)](
            bool allowed, const nlohmann::json& disablePatch) {
            setEnabled(true);
            auto* currentModel = app_->filterModel();
            if (owner == nullptr || !isActiveSettingTarget(target) || currentModel == nullptr ||
                currentModel->filterUuid() != target.filterUuid) {
                return;
            }
            if (!allowed && !disablePatch.empty()) {
                allowed = resolveEffectConflicts(disablePatch);
            }
            if (owner != nullptr && isActiveSettingTarget(target)) {
                complete(allowed);
            }
        });
}

bool SettingController::resolveEffectConflicts(const nlohmann::json& disablePatch) {
    if (!disablePatch.is_array() || disablePatch.empty()) {
        LogError << "Missing conflict disable patch" << logEnd;
        assert(false);
        return false;
    }

    const std::u8string filterUuid = selectedCiciFilterUuid();
    if (filterUuid.empty() || app_->filterModel() == nullptr) {
        return false;
    }

    std::set<int> affectedPages;
    for (const auto& operation : disablePatch) {
        if (!operation.is_object() || !operation.contains("op") || operation["op"] != "add" ||
            !operation.contains("path") || !operation["path"].is_string() || !operation.contains("value")) {
            LogError << "Invalid conflict disable operation" << logEnd;
            assert(false);
            return false;
        }
        const int page = settingPageFromPath(operation["path"].get_ref<const std::string&>());
        if (page < 0) {
            LogError << "Unknown conflict settings page, path=" << operation["path"].get<std::string>() << logEnd;
            assert(false);
            return false;
        }
        affectedPages.insert(page);
    }

    const auto answer = QMessageBox::question(
        this,
        tr("Effect Conflict"),
        tr("The selected effect conflicts with enabled effects. Replace them?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return false;
    }

    if (filterUuid != selectedCiciFilterUuid() || app_->filterModel() == nullptr) {
        return false;
    }

    for (const auto& operation : disablePatch) {
        applySettingPatch(nlohmann::json::array({operation}));
    }

    const nlohmann::json resolvedConfig = app_->settingConfig()->config(filterUuid);
    for (const int page : affectedPages) {
        syncPageFromConfig(page, resolvedConfig);
    }

    return true;
}

void SettingController::switchLanguage() {
}

std::u8string SettingController::selectedSourceUuid() const {
    return to_u8(sourceCombo_->currentData().toString());
}

std::u8string SettingController::selectedCiciFilterUuid() const {
    const std::u8string sourceUuid = selectedSourceUuid();
    obs_source_t* source = obs_get_source_by_uuid(to_char(sourceUuid));
    if (source == nullptr) {
        return {};
    }

    obs_source_t* filter = ciciFilterFromSource(source);
    obs_source_release(source);
    if (filter == nullptr) {
        return {};
    }

    std::u8string filterUuid = obsString(obs_source_get_uuid(filter));
    obs_source_release(filter);
    return filterUuid;
}

void SettingController::loadSettingConfig(const std::u8string& sourceUuid) {
    const std::u8string filterUuid = selectedCiciFilterUuid();
    const bool hasFilter = !filterUuid.empty();
    auto* model = app_->filterModel();
    const std::u8string modelSourceUuid = model != nullptr
        ? model->sourceUuid()
        : std::u8string();
    const bool modelReady = model != nullptr && modelSourceUuid == sourceUuid;
    updateSettingTabsEnabled(hasFilter && modelReady);

    if (sourceUuid.empty() || !hasFilter) {
        return;
    }
    if (!modelReady) {
        LogDebug << "Setting config load deferred: filter model is not ready"
                 << " source_uuid=" << sourceUuid
                 << " model_source_uuid=" << modelSourceUuid << logEnd;
        return;
    }

    LogDebug << "Setting config load source_uuid=" << sourceUuid << logEnd;
    const nlohmann::json config = app_->settingConfig()->config(filterUuid);
    this->json2Me(config);
}

void SettingController::applySettingPatch(const nlohmann::json& patch) {
    const std::u8string sourceUuid = selectedSourceUuid();
    assert(!sourceUuid.empty());
    if (sourceUuid.empty()) {
        return;
    }

    auto* model = app_->filterModel();
    assert(model != nullptr);
    if (model == nullptr) {
        LogWarn << "SettingController::applySettingPatch ignored: filter model is null"
                << " source_uuid=" << sourceUuid << logEnd;
        return;
    }

    model->applySettingPatch(patch);
}

void SettingController::refreshAccountWidgets() {
    const auto* login = app_->loginLogic();
    if (!login->hasUsableSession()) {
        clearAccountWidgets();
        return;
    }

    const QString displayName = displayNameFromIdentity(login->nickname(), login->email(), login->uid());
    const QString avatarUrl = from_u8(login->avatarUrl());

    accountEntry_->setAccount(avatarUrl, displayName);
}

void SettingController::reloadFavoriteViews() {
    if (filterSetting_ != nullptr) {
        filterSetting_->reloadFavorites();
    }
    if (aiEffect_ != nullptr) {
        aiEffect_->reloadFavorites();
    }
    if (specialEffectsAssets_ != nullptr) {
        specialEffectsAssets_->reloadFavorites();
    }
    if (lensEffectsSetting_ != nullptr) {
        lensEffectsSetting_->reloadFavorites();
    }
}

void SettingController::clearAccountWidgets() {
    if (accountEntry_ != nullptr) {
        accountEntry_->clearAccount();
    }
}

void SettingController::replacePage(int row, QWidget* page) {
    QWidget* old = stackedWidget_->widget(row);
    stackedWidget_->removeWidget(old);
    old->deleteLater();
    stackedWidget_->insertWidget(row, page);
    if (tabBar_->currentRow() == row) {
        stackedWidget_->setCurrentIndex(row);
    }
    stackedWidget_->updateGeometry();
    updateGeometry();
}

void SettingController::syncPageFromConfig(int row, const nlohmann::json& config) {
    const auto emptyObject = nlohmann::json::object();
    switch (row) {
    case PageBasic:
        if (basicSetting_ != nullptr) {
            basicSetting_->json2Me(config.value("basic", emptyObject));
        }
        break;
    case PageFilter:
        if (filterSetting_ != nullptr) {
            filterSetting_->json2Me(config.value("filters", emptyObject));
        }
        break;
    case PageBeauty:
        if (beautyEnhancement_ != nullptr) {
            beautyEnhancement_->json2Me(config.value("beauty", emptyObject));
        }
        break;
    case PageMakeup:
        if (makeupSetting_ != nullptr) {
            makeupSetting_->json2Me(config.value("makeup", emptyObject));
        }
        break;
    case PageBodyShaping:
        if (bodyShapingSetting_ != nullptr) {
            bodyShapingSetting_->json2Me(config.value("bodyShaping", emptyObject));
        }
        break;
    case PageAIEffect:
        if (aiEffect_ != nullptr) {
            aiEffect_->json2Me(config.value("aiEffects", emptyObject));
        }
        break;
    case PageEffectAsset:
        if (specialEffectsAssets_ != nullptr) {
            specialEffectsAssets_->json2Me(config.value("spEffects", emptyObject));
        }
        break;
    case PageKeying:
        if (keyingSetting_ != nullptr) {
            keyingSetting_->json2Me(config.value("keying", emptyObject));
        }
        break;
    case PageLens:
        if (lensEffectsSetting_ != nullptr) {
            lensEffectsSetting_->json2Me(config.value("lensEffects", emptyObject));
        }
        break;
    default:
        break;
    }
}

bool SettingController::ensurePageCreated(int row) {
    HPTimer tt;
    tt.start();

    switch (row) {
    case PageBasic:
        if (basicSetting_ == nullptr) {
            basicSetting_ = new BasicSettingController(app_);
            basicSetting_->createUI();
            QObject::connect(basicSetting_, &BasicSettingController::settingPatchRequested,
                             this, &SettingController::applySettingPatch);
            replacePage(row, basicSetting_);
            LogTrace << "UI Basic Setting:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    case PageFilter:
        if (filterSetting_ == nullptr) {
            filterSetting_ = new FilterSettingController(app_, this);
            filterSetting_->createUI();
            QObject::connect(filterSetting_, &FilterSettingController::settingPatchRequested,
                             this, &SettingController::applySettingPatch);
            replacePage(row, filterSetting_);
            LogTrace << "UI Filter Setting:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    case PageBeauty:
        if (beautyEnhancement_ == nullptr) {
            beautyEnhancement_ = new BeautyEnhancementController(app_);
            beautyEnhancement_->createUI();
            QObject::connect(beautyEnhancement_, &BeautyEnhancementController::settingPatchRequested,
                             this, &SettingController::applySettingPatch);
            replacePage(row, beautyEnhancement_);
            LogTrace << "UI Beauty Enhancement:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    case PageMakeup:
        if (makeupSetting_ == nullptr) {
            makeupSetting_ = new MakeupSettingController(app_, this);
            makeupSetting_->createUI();
            QObject::connect(makeupSetting_, &MakeupSettingController::settingPatchRequested,
                             this, &SettingController::applySettingPatch);
            replacePage(row, makeupSetting_);
            LogTrace << "UI Makeup Setting:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    case PageBodyShaping:
        if (bodyShapingSetting_ == nullptr) {
            bodyShapingSetting_ = new BodyShapingSettingController(app_);
            bodyShapingSetting_->createUI();
            QObject::connect(bodyShapingSetting_, &BodyShapingSettingController::settingPatchRequested,
                             this, &SettingController::applySettingPatch);
            replacePage(row, bodyShapingSetting_);
            LogTrace << "UI Body Shaping:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    case PageAIEffect:
        if (aiEffect_ == nullptr) {
            aiEffect_ = new AIEffectController(app_);
            aiEffect_->createUI();
            QObject::connect(aiEffect_, &AIEffectController::settingPatchRequested,
                             this, &SettingController::applySettingPatch);
            replacePage(row, aiEffect_);
            LogTrace << "UI AI Effect:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    case PageEffectAsset:
        if (specialEffectsAssets_ == nullptr) {
            specialEffectsAssets_ = new SpecialEffectsAssetsController(app_);
            specialEffectsAssets_->createUI();
            QObject::connect(specialEffectsAssets_, &SpecialEffectsAssetsController::settingPatchRequested,
                             this, &SettingController::applySettingPatch);
            replacePage(row, specialEffectsAssets_);
            LogTrace << "UI Effect Asset:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    case PageKeying:
        if (keyingSetting_ == nullptr) {
            keyingSetting_ = new KeyingSettingController(app_);
            keyingSetting_->createUI();
            QObject::connect(keyingSetting_, &KeyingSettingController::settingPatchRequested,
                             this, &SettingController::applySettingPatch);
            replacePage(row, keyingSetting_);
            LogTrace << "UI Keying Setting:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    case PageLens:
        if (lensEffectsSetting_ == nullptr) {
            lensEffectsSetting_ = new LensEffectsController(app_);
            lensEffectsSetting_->createUI();
            QObject::connect(lensEffectsSetting_, &LensEffectsController::settingPatchRequested,
                             this, &SettingController::applySettingPatch);
            replacePage(row, lensEffectsSetting_);
            LogTrace << "UI Lens Effect:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    case PageImportExport:
        if (importExport_ == nullptr) {
            importExport_ = new ImportExportController(app_, this);
            importExport_->createUI();
            replacePage(row, importExport_);
            LogTrace << "UI Im&Export:" << tt.lastElapsedUS() << logEnd;
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

void SettingController::refreshSourceCombo() {
    const std::u8string previous = selectedSourceUuid();
    const auto sources = collectSourceItems();

    QSignalBlocker blocker(sourceCombo_);
    sourceCombo_->clear();

    int selected = -1;
    for (int i = 0; i < static_cast<int>(sources.size()); ++i) {
        const auto& source = sources[static_cast<size_t>(i)];
        sourceCombo_->addItem(from_u8(source.name), from_u8(source.uuid));
        if (source.uuid == previous) {
            selected = i;
        }
    }

    if (selected < 0 && sourceCombo_->count() > 0) {
        selected = 0;
    }

    sourceCombo_->setCurrentIndex(selected);
    sourceCombo_->setEnabled(sourceCombo_->count() > 0);
    LogInfo << "[SourceSelection] combo refreshed previousUuid=" << previous
            << ", index=" << sourceCombo_->currentIndex()
            << ", name=" << sourceCombo_->currentText()
            << ", uuid=" << selectedSourceUuid()
            << ", count=" << sourceCombo_->count() << logEnd;
    refreshApplyFilterButton();
}

void SettingController::refreshApplyFilterButton() {
    const std::u8string uuid = selectedSourceUuid();
    if (uuid.empty()) {
        applyFilterBtn_->setText(tr("Apply Cici"));
        applyFilterBtn_->setMinimumSize(applyFilterBtn_->sizeHint());
        applyFilterBtn_->setEnabled(false);
        return;
    }

    obs_source_t* source = obs_get_source_by_uuid(to_char(uuid));
    if (source == nullptr) {
        applyFilterBtn_->setText(tr("Apply Cici"));
        applyFilterBtn_->setMinimumSize(applyFilterBtn_->sizeHint());
        applyFilterBtn_->setEnabled(false);
        return;
    }
    if (!isCiciApplicableSource(source)) {
        obs_source_release(source);
        applyFilterBtn_->setText(tr("Apply Cici"));
        applyFilterBtn_->setMinimumSize(applyFilterBtn_->sizeHint());
        applyFilterBtn_->setEnabled(false);
        return;
    }

    const bool applied = app_->hasCiciFilterOnSource(uuid);
    obs_source_release(source);

    applyFilterBtn_->setText(applied ? tr("Remove Cici") : tr("Apply Cici"));
    applyFilterBtn_->setMinimumSize(applyFilterBtn_->sizeHint());
    applyFilterBtn_->setEnabled(true);
}

bool SettingController::isSettingTabEnabled(int row) const {
    if (row < 0 || row >= tabBar_->count()) {
        return false;
    }

    auto* item = tabBar_->item(row);
    return item != nullptr && item->flags().testFlag(Qt::ItemIsEnabled);
}

void SettingController::updateSettingTabsEnabled(bool hasFilter) {
    QSignalBlocker blocker(tabBar_);
    for (int row = 0; row < tabBar_->count(); ++row) {
        auto* item = tabBar_->item(row);
        assert(item != nullptr);
        if (item == nullptr) {
            continue;
        }

        Qt::ItemFlags flags = item->flags();
        if (hasFilter) {
            flags |= Qt::ItemIsEnabled;
            flags |= Qt::ItemIsSelectable;
            item->setToolTip(QString());
        } else {
            flags &= ~Qt::ItemIsEnabled;
            flags &= ~Qt::ItemIsSelectable;
            item->setToolTip(tr("Please apply Cici to the selected source first."));
        }
        item->setFlags(flags);
    }

    stackedWidget_->setEnabled(hasFilter);
}

void SettingController::syncSelectedFilterModel() {
    const std::u8string sourceUuid = selectedSourceUuid();
    LogInfo << "[SourceSelection] sync index=" << sourceCombo_->currentIndex()
            << ", name=" << sourceCombo_->currentText()
            << ", uuid=" << sourceUuid << logEnd;
    emit uiSelectedSourceChanged(sourceUuid);
}

void SettingController::onSourceComboCurrentIndexChanged(int index) {
    LogInfo << "[SourceSelection] currentIndexChanged index=" << index
            << ", name=" << sourceCombo_->currentText()
            << ", uuid=" << selectedSourceUuid() << logEnd;
    refreshApplyFilterButton();
    syncSelectedFilterModel();
    loadSettingConfig(selectedSourceUuid());
}

void SettingController::onApplyFilterClicked() {
    const std::u8string uuid = selectedSourceUuid();
    assert(!uuid.empty());
    if (uuid.empty()) {
        return;
    }

    if (app_->hasCiciFilterOnSource(uuid)) {
        app_->removeCiciFilterFromSource(uuid, u8"user_click");
    } else {
        app_->applyCiciFilterToSource(uuid, u8"user_click");
    }
}

void SettingController::connectObsSignals() {
    if (obsSignalHandler_ != nullptr) {
        return;
    }

    obsSignalHandler_ = obs_get_signal_handler();
    if (obsSignalHandler_ == nullptr) {
        return;
    }

    signal_handler_connect_ref(obsSignalHandler_, "source_create", handleObsSourceListSignal, this);
    signal_handler_connect_ref(obsSignalHandler_, "source_destroy", handleObsSourceListSignal, this);
    signal_handler_connect_ref(obsSignalHandler_, "source_remove", handleObsSourceListSignal, this);
    signal_handler_connect_ref(obsSignalHandler_, "source_rename", handleObsSourceListSignal, this);
}

void SettingController::disconnectObsSignals() {
    if (obsSignalHandler_ == nullptr) {
        return;
    }

    signal_handler_disconnect(obsSignalHandler_, "source_create", handleObsSourceListSignal, this);
    signal_handler_disconnect(obsSignalHandler_, "source_destroy", handleObsSourceListSignal, this);
    signal_handler_disconnect(obsSignalHandler_, "source_remove", handleObsSourceListSignal, this);
    signal_handler_disconnect(obsSignalHandler_, "source_rename", handleObsSourceListSignal, this);
    obsSignalHandler_ = nullptr;
}

void SettingController::handleObsSourceListSignal(void *data, calldata_t *cd) {
    logObsCallbackThread("SettingController::handleObsSourceListSignal");
    (void)cd;

    QPointer<SettingController> ctrl = static_cast<SettingController *>(data);
    if (isMainThread()) {
	    ctrl->onObsSourceListChanged();
    } else {
	    QMetaObject::invokeMethod(
		    ctrl,
		    [ctrl]() {
			    assert(isMainThread());
			    if (ctrl != nullptr) {
				    ctrl->onObsSourceListChanged();
			    }
		    },
		    Qt::QueuedConnection);
    }

}

void SettingController::onObsSourceListChanged() {
    const std::u8string oldSourceUuid = selectedSourceUuid();

    refreshSourceCombo();

    const std::u8string newSourceUuid = selectedSourceUuid();
    if (newSourceUuid == oldSourceUuid) {
        return;
    }
    
    syncSelectedFilterModel();
    loadSettingConfig(newSourceUuid);
}

void SettingController::onCiciFilterChanged(const std::u8string& sourceUuid) {
    if (sourceUuid != selectedSourceUuid()) {
        return;
    }

    refreshApplyFilterButton();
    syncSelectedFilterModel();

    loadSettingConfig(sourceUuid);
}

void SettingController::onTabCurrentRowChanged(int row) {
    assert(isSettingTabEnabled(row));
    const std::u8string filterUuid = selectedCiciFilterUuid();
    assert(!filterUuid.empty());
    if (filterUuid.empty()) {
        return;
    }

    nlohmann::json config = app_->settingConfig()->config(filterUuid);
    config["version"] = 1;
    config["selected"] = row;
    app_->settingConfig()->applyPatch(filterUuid, JsonUtility::Patch::add(u8"/selected", row));

    const bool created = ensurePageCreated(row);
    if (created) {
        syncPageFromConfig(row, config);
    }
    if (row >= 0 && row < stackedWidget_->count()) {
        stackedWidget_->setCurrentIndex(row);
    }
}

void SettingController::onLoginSuccess() {
    refreshAccountWidgets();
    loadSettingConfig(selectedSourceUuid());
    reloadFavoriteViews();
    updateAccessOverlay();
}

void SettingController::onLoginFailed(const std::u8string& error) {
    (void)error;

    if (app_->loginLogic()->hasUsableSession()) {
        return;
    }

    reloadFavoriteViews();
    clearAccountWidgets();
    QMetaObject::invokeMethod(this, [this]() {
        updateAccessOverlay();
    }, Qt::QueuedConnection);
}

void SettingController::onLogouted() {
    reloadFavoriteViews();
    clearAccountWidgets();
    QMetaObject::invokeMethod(this, [this]() {
        updateAccessOverlay();
    }, Qt::QueuedConnection);
}

/*bool SettingController::eventFilter(QObject *watched, QEvent *event) {
    if (watched != qqService_ && watched != guide_) {
        return QWidget::eventFilter(watched, event);
    }
    
    if (event->type() == QEvent::Enter) {
        if (watched == qqService_) {
            qqService_->setCursor(Qt::PointingHandCursor);
        }
        else if (watched == guide_) {
            guide_->setCursor(Qt::PointingHandCursor);
        }
        else {
            assert(false);
        }
        
        return true;
    }
    else if (event->type() == QEvent::Leave) {
        if (watched == qqService_) {
            qqService_->setCursor(Qt::ArrowCursor);
        }
        else if (watched == guide_) {
            guide_->setCursor(Qt::ArrowCursor);
        }
        else {
            assert(false);
        }
        
        return true;
    }
    else if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            if (watched == qqService_) {
                reportSupportUiAction(SupportUiAction::QqServiceClick);
                //todo...
            }
            else if (watched == guide_) {
                reportSupportUiAction(SupportUiAction::GuideClick);
                //todo...
            }
            else {
                assert(false);
            }
            return true;
        }
    }
    
    return QWidget::eventFilter(watched, event);
}*/

void SettingController::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (accessOverlay_ != nullptr) {
        updateAccessOverlay();
    }
}

void SettingController::showEvent(QShowEvent* event) {
    CiciWidget::showEvent(event);
    if (accessOverlay_ != nullptr) {
        updateAccessOverlay();
    }
}

SettingTarget SettingController::activeSettingTarget() const {
    SettingTarget target;
    target.sourceUuid = selectedSourceUuid();
    if (!target.sourceUuid.empty()) {
        target.filterUuid = selectedCiciFilterUuid();
    }
    return target;
}

bool SettingController::isActiveSettingTarget(const SettingTarget& target) const {
    if (!target.isValid()) {
        return false;
    }

    const SettingTarget activeTarget = activeSettingTarget();
    return target.sourceUuid == activeTarget.sourceUuid &&
           target.filterUuid == activeTarget.filterUuid;
}

nlohmann::json SettingController::activeSettingConfig(const SettingTarget& target) const {
    if (!isActiveSettingTarget(target)) {
        return nlohmann::json::object();
    }

    nlohmann::json config = app_->settingConfig()->config(target.filterUuid);
    config["version"] = 1;
    config["selected"] = tabBar_->currentRow();
    return config;
}

bool SettingController::importActiveSettingConfig(
    const SettingTarget& target,
    nlohmann::json config,
    const nlohmann::json& favorites) {
    if (!isActiveSettingTarget(target)) {
        return false;
    }

    auto* model = app_->filterModel();
    if (model == nullptr) {
        return false;
    }

    auto* settingConfig = app_->settingConfig();
    if (!settingConfig->replaceFavorites(favorites)) {
        return false;
    }

    config["version"] = 1;
    config["selected"] = tabBar_->currentRow();
    if (!model->replaceSettingConfig(config)) {
        return false;
    }
    json2Me(config);
    reloadFavoriteViews();
    return true;
}

void SettingController::reset() {
    resetState(true);
}

void SettingController::resetState(bool resetChildren) {
    assert(!selectedCiciFilterUuid().empty());
    tabBar_->setCurrentRow(0);
    ensurePageCreated(0);

    if (resetChildren) {
        if (basicSetting_ != nullptr) {
            basicSetting_->reset();
        }
        if (filterSetting_ != nullptr) {
            filterSetting_->reset();
        }
        if (beautyEnhancement_ != nullptr) {
            beautyEnhancement_->reset();
        }
        if (makeupSetting_ != nullptr) {
            makeupSetting_->reset();
        }
        if (bodyShapingSetting_ != nullptr) {
            bodyShapingSetting_->reset();
        }
        if (aiEffect_ != nullptr) {
            aiEffect_->reset();
        }
        if (specialEffectsAssets_ != nullptr) {
            specialEffectsAssets_->reset();
        }
        if (keyingSetting_ != nullptr) {
            keyingSetting_->reset();
        }
        if (lensEffectsSetting_ != nullptr) {
            lensEffectsSetting_->reset();
        }
    }
}

void SettingController::json2Me(const nlohmann::json& j) {
    assert(!selectedCiciFilterUuid().empty());
    const auto empty = nlohmann::json::object();
    const auto& source = j.is_object() ? j : empty;

    uint32_t selected = source.value("selected", 0);
    if (selected >= static_cast<uint32_t>(tabBar_->count())) {
        selected = 0;
    }

    {
        QSignalBlocker blocker(tabBar_);
        tabBar_->setCurrentRow(static_cast<int>(selected));
    }
    assert(isSettingTabEnabled(static_cast<int>(selected)));
    ensurePageCreated(static_cast<int>(selected));
    if (selected < static_cast<uint32_t>(stackedWidget_->count())) {
        stackedWidget_->setCurrentIndex(static_cast<int>(selected));
    }

    for (int row = 0; row < tabBar_->count(); ++row) {
        syncPageFromConfig(row, source);
    }
}
