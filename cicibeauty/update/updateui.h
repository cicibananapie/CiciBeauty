// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <assert.h>
#include <map>
#include <vector>
#include <string>

#include <QDialog>
#include <QMetaObject>
#include <QPointer>

#include "update_common.h"

class QPushButton;
class QStackedWidget;
class QProgressBar;
class QTimer;
class QLabel;
class QPixmap;
class GlobalModel;
class QKeyEvent;
class QCloseEvent;
class RemoteImageStatus;
class UpdateDialog: public QDialog {
    Q_OBJECT
public:
    typedef std::vector<std::pair<std::u8string, std::u8string>> Feature;
    
    explicit UpdateDialog(GlobalModel *app, QWidget *parent = nullptr);
    
    void setNewVersion(const std::u8string& version);
    inline void setNewVersion(const char8_t* version) { setNewVersion(version ? std::u8string(version) : std::u8string()); }
    const std::u8string& newVersionU8() const { return newVersion_; }
    void setUpdateType(UpdateType type) {type_ = type;}
    UpdateType updateType() const {return type_;}
    //first:text, second:image
    void setupUI(const Feature& features);
    void setProgress(int p);
    void setCancelling();
    void setUpdateFinishResult(bool suc);
    
signals:
    void contentReady();
    void startUpdateRequested();
    void cancelUpdateRequested();
    void restartOBSRequested();
    void closeUpdateDialogRequested();
    
protected:
    void keyPressEvent(QKeyEvent* event) override;
	void closeEvent(QCloseEvent* event) override;
    
private:
    void setupBaseUI();
    void beginImagePreparation();
    bool allRemoteImagesFinished() const;
    void finalizeContent();
    void buildDescriptionPages();
    void startCarouselIfNeeded();
    QPixmap resolveImage(const std::u8string& image) const;

private:
    GlobalModel *app_ = nullptr;
    QPushButton* primaryBtn_ = nullptr;
    QPushButton* secondaryBtn_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* resultLabel_ = nullptr;
    QTimer* imageWaitTimer_ = nullptr;
    QMetaObject::Connection primaryBtnConnection_;
    QMetaObject::Connection secondaryBtnConnection_;
    std::vector<QMetaObject::Connection> remoteImageConnections_;
    std::map<std::u8string, QPointer<RemoteImageStatus>> remoteImages_;
    Feature features_;
    bool contentFinalized_ = false;
    UpdateType type_ = UpdateType::NoNeedUpdate;
    std::u8string newVersion_;
};
