// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <QLabel>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "nlohmann/json.hpp"
#include "commonui.h"
#include "basicsettingcontroller.h"
#include "jsonutility.h"
#include "../statistics/settingspageanalytics.h"

BasicSettingController::BasicSettingController(GlobalModel *app, QWidget *parent /* = nullptr*/)
	: CiciWidget(parent), app_(app) {
    assert(app_ != nullptr);
}

void BasicSettingController::createUI() {
    QLabel* camera = createLabel(tr("Camera:"));
    QLabel* cameraName = createVideoInputLabel(app_, tr("No Camera"));
    QHBoxLayout* cameraLayout = new QHBoxLayout;
    cameraLayout->setSpacing(KSpacing);
    cameraLayout->addWidget(camera);
    cameraLayout->addWidget(cameraName);
    cameraLayout->addStretch();

    enableFacePoints_ = createCheckBox(tr("Enable face points"));
    QHBoxLayout* facePointsLayout = new QHBoxLayout;
    facePointsLayout->setSpacing(KSpacing);
    facePointsLayout->addWidget(enableFacePoints_);
    facePointsLayout->addStretch();
    
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(cameraLayout);
    vbox->addItem(facePointsLayout);
    vbox->addStretch();
    
    this->setLayout(vbox);
    
    QObject::connect(enableFacePoints_, &QCheckBox::stateChanged, this, &BasicSettingController::onFacePointsStateChanged);
    QObject::connect(enableFacePoints_, &QCheckBox::clicked, this, [this](bool checked) {
        reportBasicFacePointsToggle(app_, checked);
    });
}

BasicSettingController::~BasicSettingController() {
}

void BasicSettingController::switchLanguage() {
}

void BasicSettingController::onFacePointsStateChanged(int state) {
    emit settingPatchRequested(JsonUtility::Patch::add(u8"/basic/enableFacePoints", state == Qt::Checked));
}

void BasicSettingController::reset() {
    QSignalBlocker blocker(enableFacePoints_);
    enableFacePoints_->setCheckState(Qt::Unchecked);
}

void BasicSettingController::json2Me(const nlohmann::json& j) {
    QSignalBlocker blocker(enableFacePoints_);
    enableFacePoints_->setCheckState(j.value("enableFacePoints", false) ? Qt::Checked : Qt::Unchecked);
}
