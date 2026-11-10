// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>
#include <vector>
#include <QWidget>

#include "ciciwidget.h"
#include "../modelapp.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QLineEdit;
class QRadioButton;
class QButtonGroup;
class QStackedWidget;
class QVBoxLayout;
class LegZoneEditorWidget;
class LegPreviewWidget;
class GlobalModel;

class ManualLengthenLegController: public CiciWidget {
    Q_OBJECT
    
    enum AxialDeformationType {
        UpDirection = 0,
        DownDirection = 1,
        BiDirection = 2
    };
    
    struct ZoneState {
        int id = -1;
        float top = 0.0f;
        float bottom = 0.0f;
        int sliders[3] = {50, 50, 50};
        AxialDeformationType direction = DownDirection;
        
        int currentSlider() const {
            return sliders[static_cast<int>(direction)];
        }
        void setCurrentSlider(int value) {
            sliders[static_cast<int>(direction)] = value;
        }
    };
    
    struct CtrlGroup {
        QLabel* zoneLabel_ = nullptr;
        QLabel* directionLabel_ = nullptr;
        QRadioButton* upRadio_ = nullptr;
        QRadioButton* downRadio_ = nullptr;
        QRadioButton* biDirectionRadio_ = nullptr;
        QButtonGroup* bg_ = nullptr;
        QLabel* shortenLabel_ = nullptr;
        QSlider* slider_ = nullptr;
        QLabel* lengthenLabel_ = nullptr;
        
        void setEnabled(bool enabled);
    };
    
public:
    ManualLengthenLegController(GlobalModel *app, QWidget *parent = nullptr);
    ~ManualLengthenLegController();
    
public:
    void json2Me(const nlohmann::json& j) override;
    nlohmann::json me2Json() const;

signals:
    void stateChanged(const QString& uiAction);
    void uiActionTriggered(const QString& action);
    
private slots:
    void onAddBtnClicked();
    void onRemoveBtnClicked();
    void onResetBtnClicked();
    void onEditorZonesDirty();
    void onZoneSelected(int id);
    void onZoneDeleteRequested(int id);
    void onPreviewOffsetRequested(float offset);
    
private:
    void createCtrlGroup(QVBoxLayout* layout);
    ZoneState* findZone(int id);
    const ZoneState* findZone(int id) const;
    ZoneState* selectedZone();
    void syncZonesFromEditor(bool preserveZoneState, const QString& uiAction = {});
    void updateCtrlGroup();
    void updatePreview();
    void updatePreviewOffset();
    static float previewHeightDeltaForZone(const ZoneState& zone);
    void syncPreviewOffsetToMRE();
    void emitStateChanged(const QString& uiAction = {});
    
private:
    static int generateSolutionId();
    
private:
    std::vector<ZoneState> zones_;
    int selectedZoneId_ = -1;
    float previewOffset_ = 0.0f;
    float previewMinOffset_ = 0.0f;
    float previewMaxOffset_ = 0.0f;
    int solutionId_ = 0;
    CtrlGroup ctrl_;
    QPushButton* removeBtn_ = nullptr;
    LegZoneEditorWidget* zoneEditor_ = nullptr;
    LegPreviewWidget* previewWidget_ = nullptr;
    GlobalModel *app_ = nullptr;
    bool loadingConfig_ = false;
    
private:
    static int nextSolutionId_;
};

class BodyShapingSolutionController: public CiciWidget {
    Q_OBJECT
    
public:
    BodyShapingSolutionController(GlobalModel *app, QWidget *parent = nullptr);
    ~BodyShapingSolutionController();
    QString customName() const;
    QString displayName() const;
    void setCustomName(const QString& customName);
    
public:
    void createUI();
    
public:
    void json2Me(const nlohmann::json& j) override;
    
signals:
    void fieldChanged(const QString& key, const nlohmann::json& value);
    void fieldChangedWithAction(const QString& key,
                                const nlohmann::json& value,
                                const QString& uiAction);
    
private:
    void bindSliderEditor(QSlider* slider, QLineEdit* editor, const char* key);
    
private:
    QSlider* slimBodySlider_ = nullptr;
    QLineEdit* slimBodyEditor_ = nullptr;
    QSlider* smallHeadSlider_ = nullptr;
    QLineEdit* smallHeadEditor_ = nullptr;
    QSlider* longLegSlider_ = nullptr;
    QLineEdit* longLegEditor_ = nullptr;
    QRadioButton* manualLeg_ = nullptr;
    QRadioButton* AILeg_ = nullptr;
    QWidget* aiPage_ = nullptr;
    QWidget* manualPage_ = nullptr;
    ManualLengthenLegController* manualPopup_ = nullptr;
    QStackedWidget* stackedWidget_ = nullptr;
    QString name_;
    GlobalModel *app_ = nullptr;
    bool loadingConfig_ = false;
};

class QComboBox;
class QTabWidget;
class BodyShapingSettingController: public CiciWidget {
    Q_OBJECT
    
public:
    BodyShapingSettingController(GlobalModel *app, QWidget *parent = nullptr);
    ~BodyShapingSettingController();
    
public:
    void createUI();
    
public:
    void reset() override;
    void json2Me(const nlohmann::json& j) override;

signals:
    void settingPatchRequested(const nlohmann::json& patch);
    
private:
    void refreshEnableState();
    void resetState(bool resetSolutions);
    bool isSolutionTab(int index) const;
    int solutionTabCount() const;
    int selectedSolutionIndexAfterRemove(int removedIndex) const;
    void refreshNewSolutionTabCloseButton();
    void emitSolutionFieldPatch(BodyShapingSolutionController* solution,
                                const QString& key,
                                const nlohmann::json& value,
                                const QString& uiAction = {});
    
private slots:
    void onEnableMakeupStateChanged(int);
    void onTabCloseRequested(int index);
    void onTabBarClicked(int index);
    void onTabBarDoubleClicked(int index);
    void onCurrentChanged(int);
    
public:
    void switchLanguage();
    
private:
    QCheckBox* enableBodyShaping_ = nullptr;
    QTabWidget* tabWidget_ = nullptr;
    QWidget* newSlnWidget_ = nullptr;
    GlobalModel *app_ = nullptr;
    bool loadingConfig_ = false;
};
