// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "updateui.h"

#include <filesystem>

#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QProgressBar>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QTimer>
#include <QVBoxLayout>

#include "misc.h"
#include "log.h"
#include "misc.h"
#include "../modelapp.h"
#include "../qstringconv.h"
#include "../ui/assetsmgr.h"
#include "../ui/commonui.h"
#include "updatedescriptionpages.h"

namespace {

constexpr int kUpdateDialogWidth = 600;
constexpr int kUpdateDialogHeight = 400;
constexpr int kUpdateDialogMargin = 12;
constexpr int kUpdateImageWaitTimeoutMs = 30 * 1000;
constexpr int kCarouselIntervalMs = 3 * 1000;

} // namespace

UpdateDialog::UpdateDialog(GlobalModel *app, QWidget *parent) : QDialog(parent), app_(app) {
    setFixedSize(kUpdateDialogWidth, kUpdateDialogHeight);
    setWindowTitle(tr("Update Cici Beauty"));
    applyCiciWindowIcon(this);
}

void UpdateDialog::setNewVersion(const std::u8string& version) {
    newVersion_ = version;
    setWindowTitle(tr("Update Cici Beauty to %1").arg(from_u8(newVersion_)));
}

void UpdateDialog::setupUI(const Feature& features) {
    assert(type_ == UpdateType::ForceUpdate
           ||type_ == UpdateType::PromptedBackgroundUpdate
           ||type_ == UpdateType::PromptedForegroundUpdate);
    
    if (features.empty()) {
        LogError << "no feature" << logEnd;
    }

    features_ = features;
    setupBaseUI();
    beginImagePreparation();
}

void UpdateDialog::setupBaseUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(kUpdateDialogMargin,
                                   kUpdateDialogMargin,
                                   kUpdateDialogMargin,
                                   kUpdateDialogMargin);
    stack_ = new QStackedWidget(this);
    mainLayout->addWidget(stack_, 1);

    if (type_ == UpdateType::ForceUpdate
        || type_ == UpdateType::PromptedForegroundUpdate) {
        progressBar_ = new QProgressBar(this);
        progressBar_->setRange(0, 100);
        progressBar_->setTextVisible(true);
        mainLayout->addWidget(progressBar_);
    }

    resultLabel_ = new QLabel(this);
    resultLabel_->setWordWrap(true);
    resultLabel_->hide();
    mainLayout->addWidget(resultLabel_);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    primaryBtn_ = new QPushButton(tr("Start"), this);
    secondaryBtn_ = new QPushButton(tr("Cancel"), this);
    
    btnLayout->addWidget(primaryBtn_);
    btnLayout->addWidget(secondaryBtn_);
    mainLayout->addLayout(btnLayout);

    primaryBtnConnection_ = connect(primaryBtn_, &QPushButton::clicked, this, [this](){
        primaryBtn_->setDisabled(true);
        emit startUpdateRequested();
    });
    
    secondaryBtnConnection_ = connect(secondaryBtn_, &QPushButton::clicked, this, [this](){
        secondaryBtn_->setDisabled(true);
        emit cancelUpdateRequested();
    });

    if (type_ == UpdateType::ForceUpdate) {
	    secondaryBtn_->hide();
    }
}

void UpdateDialog::beginImagePreparation() {
    for (const auto& feature : features_) {
        const auto& image = feature.second;
        if (image.empty() || !UrlOp::isUrl(image)
            || remoteImages_.find(image) != remoteImages_.end()) {
            continue;
        }

        auto* status = app_->assets()->requestRemoteImage(from_u8(image));
        remoteImages_.emplace(image, status);
        if (status->state() == RemoteImageState::Loading) {
            remoteImageConnections_.push_back(connect(
                status,
                &RemoteImageStatus::stateChanged,
                this,
                [this](RemoteImageState) {
                    if (allRemoteImagesFinished()) {
                        finalizeContent();
                    }
                }));
        }
    }

    if (allRemoteImagesFinished()) {
        finalizeContent();
        return;
    }

    imageWaitTimer_ = new QTimer(this);
    imageWaitTimer_->setSingleShot(true);
    imageWaitTimer_->setInterval(kUpdateImageWaitTimeoutMs);
    connect(imageWaitTimer_, &QTimer::timeout, this, [this]() {
        LogWarn << "update image preparation timed out" << logEnd;
        finalizeContent();
    });
    imageWaitTimer_->start();
}

bool UpdateDialog::allRemoteImagesFinished() const {
    for (const auto& entry : remoteImages_) {
        const auto& status = entry.second;
        if (status != nullptr && status->state() == RemoteImageState::Loading) {
            return false;
        }
    }
    return true;
}

void UpdateDialog::finalizeContent() {
    if (contentFinalized_) {
        return;
    }
    contentFinalized_ = true;

    if (imageWaitTimer_ != nullptr) {
        imageWaitTimer_->stop();
    }
    for (const auto& connection : remoteImageConnections_) {
        QObject::disconnect(connection);
    }
    remoteImageConnections_.clear();

    buildDescriptionPages();
    startCarouselIfNeeded();
    emit contentReady();
}

QPixmap UpdateDialog::resolveImage(const std::u8string& image) const {
    if (image.empty()) {
        return {};
    }

    QString path;
    if (UrlOp::isUrl(image)) {
        const auto it = remoteImages_.find(image);
        if (it == remoteImages_.end() || it->second == nullptr
            || it->second->state() != RemoteImageState::Ready) {
            return {};
        }
        path = it->second->localPath();
    } else {
        std::filesystem::path localPath(image);
        if (localPath.is_relative()) {
            localPath = FileSystemOp::resourcesPath() / localPath;
        }
        path = from_path(localPath);
    }

    QPixmap pixmap;
    if (!pixmap.load(path)) {
        LogWarn << "update image load failed, image:" << image << logEnd;
        return {};
    }
    return pixmap;
}

void UpdateDialog::buildDescriptionPages() {
    std::vector<UpdateDescriptionDisplayItem> descriptions;
    descriptions.reserve(features_.size());
    for (const auto& feature : features_) {
        descriptions.push_back({from_u8(feature.first), resolveImage(feature.second)});
    }
    populateUpdateDescriptionPages(stack_, descriptions);
}

void UpdateDialog::startCarouselIfNeeded() {
    startUpdateDescriptionCarousel(stack_, this, kCarouselIntervalMs);
}

void UpdateDialog::setProgress(int p) {
    if (progressBar_ != nullptr) {
        progressBar_->setValue(p);
    }
}

void UpdateDialog::setCancelling() {
    primaryBtn_->setDisabled(true);
    secondaryBtn_->setDisabled(true);
    resultLabel_->setText(tr("Cancelling update..."));
    resultLabel_->show();
}

void UpdateDialog::setUpdateFinishResult(bool suc) {
    primaryBtn_->setEnabled(true);
    secondaryBtn_->setEnabled(true);
    resultLabel_->setText(suc ? tr("Update completed") : tr("Update failed. Please try again."));
    resultLabel_->show();

    QObject::disconnect(primaryBtnConnection_);
    QObject::disconnect(secondaryBtnConnection_);

    if (suc) {
        primaryBtn_->setText(tr("Restart OBS"));
        secondaryBtn_->setText(tr("OK"));
        secondaryBtn_->setVisible(type_ != UpdateType::ForceUpdate);
        primaryBtnConnection_ = connect(primaryBtn_, &QPushButton::clicked, this, [this]() {
            primaryBtn_->setDisabled(true);
            emit restartOBSRequested();
        });
    } else {
        primaryBtn_->setText(tr("Retry"));
        secondaryBtn_->setText(tr("Close"));
        secondaryBtn_->show();
        primaryBtnConnection_ = connect(primaryBtn_, &QPushButton::clicked, this, [this]() {
            primaryBtn_->setDisabled(true);
            secondaryBtn_->setDisabled(true);
            resultLabel_->hide();
            if (progressBar_ != nullptr) {
                progressBar_->setValue(0);
            }
            emit startUpdateRequested();
        });
    }

    secondaryBtnConnection_ = connect(secondaryBtn_, &QPushButton::clicked, this, [this](){
        secondaryBtn_->setDisabled(true);
        emit closeUpdateDialogRequested();
    });
}

void UpdateDialog::keyPressEvent(QKeyEvent *event) {
	if (event->key() == Qt::Key_Escape) {
		event->accept();
		return;
    }

    QDialog::keyPressEvent(event);
}

void UpdateDialog::closeEvent(QCloseEvent *event) {
	if (event->spontaneous()) {
		event->ignore();
	} else {
		event->accept();
    }
}
