// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>

#include <obs-frontend-api.h>
#if CICI_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#elif CICI_OS_WINDOWS
#include <Windows.h>
#else
#error "Unsupported platform"
#endif

#include "log.h"
#include "ciciconfigure.h"
#include "misc.h"
#include "binarymsg.h"
#include "ipcwrapper.h"
#include "randwrapper.h"
#include "updateclient.h"
#include "updateui.h"
#include "../i18nsupport.h"
#include "../lumaclient.h"
#include "../qstringconv.h"
#include "protocol/helpers.h"
#include "../statistics/updateanalytics.h"
#include "../ui/commonui.h"

#if CICI_OS_MACOS
void makeOBSSuicide() {
    void* main_window_ptr = obs_frontend_get_main_window();
    if (main_window_ptr) {
        QWidget* main_window = static_cast<QWidget*>(main_window_ptr);
        main_window->close();
    } else {
        LogError <<"obs_frontend_get_main_window failed"<<logEnd;
    }
}

std::u8string obsBinPath() {
    char path[PATH_MAX] = {0};
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    CFURLRef url = CFBundleCopyBundleURL(mainBundle);
    CFStringRef pathString = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
    CFStringGetCString(pathString, path, sizeof(path), kCFStringEncodingUTF8);

    CFRelease(pathString);
    CFRelease(url);
    
    return to_u8(path);
}
#elif CICI_OS_WINDOWS
void makeOBSSuicide() {
    void* main_window_ptr = obs_frontend_get_main_window();
    if (main_window_ptr) {
        QWidget* main_window = static_cast<QWidget*>(main_window_ptr);
        main_window->close();
    } else {
        LogError <<"obs_frontend_get_main_window failed"<<logEnd;
    }
	HWND h = reinterpret_cast<HWND>(obs_frontend_get_main_window_handle());
	PostMessage(h, WM_CLOSE, 0, 0);
}

std::u8string obsBinPath() {
	wchar_t tmp[MAX_PATH] = {0};
    ::GetModuleFileNameW(NULL, tmp, MAX_PATH);

    return wideToUtf8(tmp);
}
#else
#error "Unsupported platform"
#endif

UpdateClient::UpdateClient(GlobalModel *app) : app_(app) {
    key_ = to_u8(ProcessThreadOp::pid());
    key_ += to_u8(RandomGenerator::randOnce(10000, 99999));
    ipc_ = new IPCServer(key_.c_str());
    ipc_->addCallback(this);
    ipc_->start();
    LumaClient::instance().addListener(this, [this](const cici::protocol::JsonMessage& message) {
        const auto& body = message.value["body"];
        if (message.value["method"] == "helpers.startFailed" && body.value("helper", "") == "updater" &&
            body.value("ipcName", std::string()) == to_char(key_)) {
            LogError << "luma could not start updater" << logEnd;
            onClosed();
        }
    }, {});
}

UpdateClient::~UpdateClient() {
    LumaClient::instance().removeListener(this);
    ipc_->removeCallback(this);
    delete ipc_;
    ipc_ = nullptr;
    if (dlg_ != nullptr) {
        delete dlg_;
    }
}

void UpdateClient::check() {
    LogInfo<<logEnd;
    activeUpdateType_ = UpdateType::NoNeedUpdate;
    targetVersion_.clear();
    updateAttempt_ = 0;
    checkResultReported_ = false;
    if (!LumaClient::instance().postMsg(cici::protocol::startUpdater(
            key_, std::u8string(CICI_VERSION), to_u8(I18nSupport::currentLocaleName())))) {
        LogError << "send updater launch request failed" << logEnd;
        onClosed();
    }
}

void UpdateClient::startUpdate(UpdateType type) {
    LogInfo<<logEnd;
    StartUpdateMsg msg;
    msg.type_ = type;

    const uint32_t attempt = updateAttempt_ + 1;
    if (ipc_->postMsg(msg)) {
        activeUpdateType_ = type;
        updateAttempt_ = attempt;
        updateInProgress_ = true;
        cancelPending_ = false;
        reportUpdateStart(type, targetVersion_, attempt);
    } else {
        reportUpdateResult(UpdateResult::StartRequestFailed,
                           type,
                           targetVersion_,
                           attempt);
    }
}

void UpdateClient::ignoreUpdate(UpdateType type) {
    LogInfo<<logEnd;
    IgnoreUpdateMsg msg;
    msg.type_ = type;
    
    ipc_->postMsg(msg);
}

bool UpdateClient::cancelUpdate(UpdateType type) {
    LogInfo << "type=" << static_cast<int>(type) << logEnd;
    CancelUpdateMsg msg;
    msg.type_ = type;
    return ipc_->postMsg(msg);
}

void UpdateClient::onConnected() {
    LogInfo<<logEnd;
}

void UpdateClient::onClosed() {
    LogInfo<<logEnd;
    if (!checkResultReported_) {
        checkResultReported_ = true;
        reportUpdateCheckResult(UpdateCheckResult::Failed,
                                UpdateType::NoNeedUpdate,
                                u8"");
    }
    if (updateInProgress_) {
        reportUpdateResult(UpdateResult::Interrupted,
                           activeUpdateType_,
                           targetVersion_,
                           updateAttempt_);
    }
    updateInProgress_ = false;
    if (cancelPending_ && dlg_ != nullptr) {
        cancelPending_ = false;
        destroyUI(0);
    }
}

void UpdateClient::onIPCMsg(const BinaryStream& rawMsg) {
    uint32_t id = BinaryUnpack::whitchMsg(rawMsg);
    LogTrace<<id<<logEnd;
    if (id == kMsgUpdateProgress) {
        UpdateProgressMsg msg;
        msg.fromBinary(rawMsg);
        handleUpdateProgressMsg(msg);
    }
    else if (id == kMsgUpdateInfo) {
        UpdateInfoMsg msg;
        msg.fromBinary(rawMsg);
        handleUpdateInfoMsg(msg);
    }
    else if (id == kMsgUpdateFinished) {
        UpdateFinishedMsg msg;
        msg.fromBinary(rawMsg);
        handleUpdateFinishMsg(msg);
    }
}

void UpdateClient::handleUpdateInfoMsg(const UpdateInfoMsg& msg) {
    LogInfo << "type=" << msg.type_ << logEnd;
    targetVersion_ = msg.version();
    activeUpdateType_ = msg.type_;
    updateAttempt_ = 0;
    if (msg.type_ == UpdateType::NoNeedUpdate) {
        checkResultReported_ = true;
        reportUpdateCheckResult(UpdateCheckResult::NoUpdate,
                                msg.type_,
                                targetVersion_);
        askUpdater2Quit();
        return;
    }

    checkResultReported_ = true;
    reportUpdateCheckResult(UpdateCheckResult::UpdateAvailable,
                            msg.type_,
                            targetVersion_);
    
    if (msg.type_ == UpdateType::SilentUpdate) {
        startUpdate(msg.type_);
        return;
    }
    
    if (dlg_ != nullptr) {
        dlg_->close();
        dlg_ = nullptr;
    }
    assert(dlg_ == nullptr);
    dlg_ = new UpdateDialog(app_, reinterpret_cast<QWidget*>(obs_frontend_get_main_window()));
    //dlg_->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    dlg_->setWindowFlags(Qt::Dialog
                         | Qt::WindowTitleHint
                         | Qt::WindowSystemMenuHint
                         | Qt::CustomizeWindowHint
                         | Qt::MSWindowsFixedSizeDialogHint);
    applyCiciWindowIcon(dlg_);

    dlg_->setAttribute(Qt::WA_StaticContents);
    dlg_->setWindowModality(Qt::ApplicationModal);
    dlg_->setAttribute(Qt::WA_DeleteOnClose);
    dlg_->setAutoFillBackground(true);

    connect(dlg_, &UpdateDialog::startUpdateRequested, this, &UpdateClient::onStartUpdateRequested);
    connect(dlg_, &UpdateDialog::cancelUpdateRequested, this, &UpdateClient::onCancelUpdateRequested);
    connect(dlg_, &UpdateDialog::restartOBSRequested, this, &UpdateClient::onRestartOBSRequested);
    connect(dlg_, &UpdateDialog::closeUpdateDialogRequested, this, &UpdateClient::onCloseUpdateDialogRequested);

    QPointer<UpdateDialog> dialog = dlg_;
    connect(dialog.data(), &UpdateDialog::contentReady, this, [this, dialog]() {
        if (dialog == nullptr || dlg_.data() != dialog.data()) {
            return;
        }
        reportUpdateUiAction(UpdateUiAction::DialogShow,
                             dialog->updateType(),
                             dialog->newVersionU8());
        dialog->show();
    });
    
    dlg_->setUpdateType(msg.type_);
    {
        auto version = msg.version();
        dlg_->setNewVersion(version);
    }
    
    UpdateDialog::Feature features;
    for (const auto& item : msg.description_) {
        auto text = item.text();
        auto image = item.image();
        auto elem = std::make_pair(text, image);
        features.push_back(elem);
    }
    dialog->setupUI(features);
}

void UpdateClient::handleUpdateProgressMsg(const UpdateProgressMsg& msg) {
    assert(msg.type_ == UpdateType::ForceUpdate
           ||msg.type_ == UpdateType::PromptedForegroundUpdate);
    
    if (dlg_ == nullptr || cancelPending_) {
        LogWarn << "ignore update progress without an active dialog" << logEnd;
        return;
    }
    float f = 0;
    if (msg.stage_ == UpdateStage::InstallPackage) {
        f = 0.6 + msg.progress_*0.4;
    }
    else {
        assert(msg.stage_ == UpdateStage::DownloadPackage);
        f = msg.progress_*0.6;
    }
    
    LogTrace<<f<<logEnd;
    dlg_->setProgress(100*f);
}

void UpdateClient::handleUpdateFinishMsg(const UpdateFinishedMsg& msg) {
    LogInfo << "type=" << msg.type_ << " result=" << static_cast<int>(msg.result_) << logEnd;
    assert(msg.type_ == UpdateType::PromptedForegroundUpdate
           ||msg.type_ == UpdateType::ForceUpdate
           ||msg.type_ == UpdateType::NoNeedUpdate
           ||msg.type_ == UpdateType::PromptedBackgroundUpdate
           ||msg.type_ == UpdateType::SilentUpdate);

    // The updater reports check failures with UpdateFinishedMsg before sending any UpdateInfoMsg.
    if (!checkResultReported_) {
        checkResultReported_ = true;
        reportUpdateCheckResult(UpdateCheckResult::Failed,
                                msg.type_,
                                targetVersion_);
        askUpdater2Quit();
        return;
    }

    UpdateResult result = UpdateResult::Failed;
    if (msg.result_ == UpdateFinishResult::Succeeded) {
        result = UpdateResult::Succeeded;
    } else if (msg.result_ == UpdateFinishResult::Cancelled) {
        result = UpdateResult::Cancelled;
    }
    reportUpdateResult(result,
                       msg.type_,
                       targetVersion_,
                       updateAttempt_);

    updateInProgress_ = false;
    cancelPending_ = false;

    if (msg.result_ == UpdateFinishResult::Cancelled) {
        if (dlg_ != nullptr) {
            destroyUI(0);
        }
        askUpdater2Quit();
        return;
    }

    if (msg.type_ == UpdateType::PromptedForegroundUpdate
        ||msg.type_ == UpdateType::ForceUpdate) {
        if (dlg_ != nullptr) {
            dlg_->setUpdateFinishResult(msg.result_ == UpdateFinishResult::Succeeded);
        }
        else {
            assert(msg.result_ == UpdateFinishResult::Failed);
            askUpdater2Quit();
        }
    }
    else {
        askUpdater2Quit();
    }
}

void UpdateClient::onStartUpdateRequested() {
    LogInfo << dlg_->updateType() << logEnd;
    assert(dlg_ != nullptr);
    reportUpdateUiAction(updateAttempt_ == 0
                             ? UpdateUiAction::StartClick
                             : UpdateUiAction::RetryClick,
                         dlg_->updateType(),
                         dlg_->newVersionU8());
    startUpdate(dlg_->updateType());
    if (dlg_->updateType() == UpdateType::PromptedBackgroundUpdate) {
        destroyUI(1);
    }
    else {
        assert(dlg_->updateType() == UpdateType::ForceUpdate
               ||dlg_->updateType() == UpdateType::PromptedForegroundUpdate);
        
    }
}

void UpdateClient::onCancelUpdateRequested() {
    assert(dlg_ != nullptr);
    const auto type = dlg_->updateType();
    LogInfo << type << logEnd;

    if (!updateInProgress_) {
        reportUpdateUiAction(UpdateUiAction::DismissClick,
                             type,
                             dlg_->newVersionU8());
        ignoreUpdate(type);
        destroyUI(0);
        return;
    }

    assert(type != UpdateType::ForceUpdate);
    reportUpdateUiAction(UpdateUiAction::CancelClick,
                         type,
                         dlg_->newVersionU8());
    if (cancelUpdate(type)) {
        cancelPending_ = true;
        dlg_->setCancelling();
    }
}

void UpdateClient::onRestartOBSRequested() {
    LogInfo<<logEnd;
    assert(dlg_ != nullptr);
    reportUpdateUiAction(UpdateUiAction::RestartClick,
                         dlg_->updateType(),
                         dlg_->newVersionU8());
    askUpdater2Quit(true, obsBinPath(), ProcessThreadOp::pid());
    destroyUI(3);

    makeOBSSuicide();
}

void UpdateClient::onCloseUpdateDialogRequested() {
    LogInfo<<logEnd;
    assert(dlg_ != nullptr);
    reportUpdateUiAction(UpdateUiAction::CloseClick,
                         dlg_->updateType(),
                         dlg_->newVersionU8());
    askUpdater2Quit();
    destroyUI(2);
}

void UpdateClient::askUpdater2Quit(bool restartOBS, const std::u8string& path, uint32_t pid) {
    LogInfo<<logEnd;
    PleaseQuitMsg msg;
    msg.restartOBS_ = restartOBS;
    msg.setPath(path);
    msg.obsPid_ = pid;
    
    ipc_->postMsg(msg);
}

void UpdateClient::destroyUI(int v) {
    dlg_->close();
    dlg_ = nullptr;
}
