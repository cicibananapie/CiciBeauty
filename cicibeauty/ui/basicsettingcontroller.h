// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QWidget>

#include "ciciwidget.h"

class QCheckBox;
class GlobalModel;
class BasicSettingController: public CiciWidget {
    Q_OBJECT

public:
    BasicSettingController(GlobalModel *app, QWidget *parent = nullptr);
    ~BasicSettingController();
    
public:
    void createUI();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void settingPatchRequested(const nlohmann::json& patch);
    
private slots:
    void onFacePointsStateChanged(int state);
    
public:
    void switchLanguage();

private:
    QCheckBox* enableFacePoints_ = nullptr;
	GlobalModel *app_ = nullptr;
};
