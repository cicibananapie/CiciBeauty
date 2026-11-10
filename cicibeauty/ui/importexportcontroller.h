// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QWidget>

class GlobalModel;
class QLineEdit;
class SettingController;
class ImportExportController: public QWidget {
    Q_OBJECT
    
public:
    ImportExportController(GlobalModel *app, SettingController *setting, QWidget *parent = nullptr);
    ~ImportExportController();
    
public:
    void createUI();
    
public:
    void switchLanguage();
    
private slots:
    void onImportBtnClicked();
    void onExportBtnClicked();
    
private:
    QLineEdit* importPath_ = nullptr;
    SettingController* setting_ = nullptr;
	GlobalModel *app_ = nullptr;
};

