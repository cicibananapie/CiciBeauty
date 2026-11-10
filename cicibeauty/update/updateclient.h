// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once
#include <cstdint>
#include <string>

#include <QObject>
#include <QPointer>

#include "update_common.h"
#include "ipcwrapper.h"

struct UpdateInfoMsg;
struct UpdateProgressMsg;
struct UpdateFinishedMsg;
class GlobalModel;
class UpdateDialog;
class UpdateClient: public QObject, public ISimpleIPCCallback {
    Q_OBJECT
public:
    UpdateClient(GlobalModel *app);
    ~UpdateClient() override;
    
public:
    void check();
    void startUpdate(UpdateType type);
    void ignoreUpdate(UpdateType type);
    bool cancelUpdate(UpdateType type);
    void askUpdater2Quit(bool restartOBS = false, const std::u8string& path=u8"", uint32_t pid = 0);
    
private:
    void handleUpdateInfoMsg(const UpdateInfoMsg& msg);
    void handleUpdateProgressMsg(const UpdateProgressMsg& msg);
    void handleUpdateFinishMsg(const UpdateFinishedMsg& msg);
    void destroyUI(int v);

private:    //ISimpleIPCCallback
    void onConnected() override;
    void onClosed() override;
    void onIPCMsg(const BinaryStream& msg) override;
    
private slots:
    void onStartUpdateRequested();
    void onCancelUpdateRequested();
    void onRestartOBSRequested();
    void onCloseUpdateDialogRequested();
    
private:
    std::u8string key_;
    IPCServer* ipc_ = nullptr;
    GlobalModel *app_ = nullptr;
    QPointer<UpdateDialog> dlg_;
    bool updateInProgress_ = false;
    bool cancelPending_ = false;
    UpdateType activeUpdateType_ = UpdateType::NoNeedUpdate;
    std::u8string targetVersion_;
    uint32_t updateAttempt_ = 0;
    bool checkResultReported_ = false;
};
