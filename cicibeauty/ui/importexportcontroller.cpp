// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <utility>

#include "nlohmann/json.hpp"
#include "log.h"
#include "misc.h"
#include "commonui.h"
#include "../qstringconv.h"
#include "../modelapp.h"
#include "../settingconfig.h"
#include "../statistics/settingsanalytics.h"
#include "importexportcontroller.h"
#include "settingcontroller.h"

namespace {

constexpr qint64 kMaxImportFileSize = 2 * 1024 * 1024;
constexpr int kTransferFormatVersion = 1;
constexpr int kSettingVersion = 1;
constexpr const char* kTransferFormat = "cici-beauty-source-config";
constexpr const char* kConfigFileSuffix = ".ciciconfig";

QString configFileFilter(bool includeAllFiles) {
    QString filter = QObject::tr("Cici Config (*%1)")
                         .arg(QString::fromLatin1(kConfigFileSuffix));
    if (includeAllFiles) {
        filter += QObject::tr(";;All (*)");
    }
    return filter;
}

QString ensureConfigSuffix(const QString& fileName) {
    if (fileName.endsWith(kConfigFileSuffix, Qt::CaseInsensitive)) {
        return fileName;
    }

    return fileName + kConfigFileSuffix;
}

void stripPathFields(nlohmann::json& config) {
    if (!config.is_object()) {
        return;
    }

    auto keying = config.find("keying");
    if (keying == config.end() || !keying->is_object()) {
        return;
    }

    keying->erase("bkFile");

    auto greenKeying = keying->find("greenKeying");
    if (greenKeying != keying->end() && greenKeying->is_object()) {
        greenKeying->erase("bkFile");
    }
}

bool integerValue(const nlohmann::json& document, const char* key, int& value) {
    auto it = document.find(key);
    if (it == document.end() || !it->is_number_integer()) {
        return false;
    }

    if (it->is_number_unsigned()) {
        const auto raw = it->get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) {
            return false;
        }
        value = static_cast<int>(raw);
        return true;
    }

    const auto raw = it->get<std::int64_t>();
    if (raw < (std::numeric_limits<int>::min)() ||
        raw > (std::numeric_limits<int>::max)()) {
        return false;
    }
    value = static_cast<int>(raw);
    return true;
}

bool parseImportedDocument(const nlohmann::json& document,
                           const nlohmann::json& defaultConfig,
                           nlohmann::json& config,
                           nlohmann::json& favorites) {
    if (!document.is_object()) {
        return false;
    }

    auto format = document.find("format");
    if (format == document.end() || !format->is_string() ||
        *format != kTransferFormat) {
        return false;
    }

    int formatVersion = 0;
    if (!integerValue(document, "formatVersion", formatVersion) ||
        formatVersion != kTransferFormatVersion) {
        return false;
    }

    auto imported = document.find("config");
    if (imported == document.end() || !imported->is_object()) {
        return false;
    }

    int settingVersion = 0;
    if (!integerValue(*imported, "version", settingVersion) ||
        settingVersion != kSettingVersion) {
        return false;
    }

    static constexpr const char* sections[] = {
        "basic", "filters", "beauty", "makeup", "bodyShaping",
        "aiEffects", "spEffects", "keying", "lensEffects"
    };

    config = defaultConfig;
    for (const char* section : sections) {
        auto value = imported->find(section);
        if (value == imported->end()) {
            continue;
        }
        if (!value->is_object()) {
            return false;
        }
        config[section] = *value;
    }

    config["version"] = kSettingVersion;
    stripPathFields(config);

    auto importedFavorites = document.find("favorites");
    if (importedFavorites == document.end() || !importedFavorites->is_object()) {
        return false;
    }
    favorites = *importedFavorites;
    return true;
}

nlohmann::json createExportDocument(nlohmann::json config,
                                    nlohmann::json favorites) {
    config["version"] = kSettingVersion;
    config.erase("selected");
    stripPathFields(config);

    return nlohmann::json::object({
        {"format", kTransferFormat},
        {"formatVersion", kTransferFormatVersion},
        {"config", std::move(config)},
        {"favorites", std::move(favorites)}
    });
}

}

ImportExportController::ImportExportController(GlobalModel *app, SettingController *setting,
					       QWidget *parent /* = nullptr*/)
	: QWidget(parent), setting_(setting), app_(app) {
}

void ImportExportController::createUI() {
    QLabel* camera = createLabel(tr("Camera:"));
    QLabel* cameraName = createVideoInputLabel(app_, tr("No Camera"));
    QHBoxLayout* titleHbox = new QHBoxLayout;
    titleHbox->addWidget(camera);
    titleHbox->addWidget(cameraName);
    titleHbox->addStretch();
    
    importPath_ = new QLineEdit;
    importPath_->setReadOnly(true);
    importPath_->setMinimumSize(importPath_->sizeHint());
    importPath_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QPushButton* importBtn = createMarqueeButton(tr("Import Config"));
    QHBoxLayout* hbox = new QHBoxLayout;
    hbox->setSpacing(KSpacing);
    hbox->addWidget(importPath_);
    hbox->addWidget(importBtn);

    auto createHintLabel = [](const QString& text) {
        QLabel* label = new QLabel(text);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        return label;
    };
    
    QLabel* label1 = createHintLabel(tr("*Importing the configuration file will overwrite the current source settings and current account favorites."));
    
    QLabel* label2 = createHintLabel(tr("*Only the configuration files exported from this plugin are supported for import."));
    
    QFrame* horizontalLine = new QFrame;
    horizontalLine->setFrameShape(QFrame::HLine);
    horizontalLine->setLineWidth(0);
    horizontalLine->setFrameShadow(QFrame::Plain);
    horizontalLine->setContentsMargins(0, 0, 0, 0);
    
    QPushButton* exportBtn = createMarqueeButton(tr("Export Config"));
    QHBoxLayout* hbox1 = new QHBoxLayout;
    hbox1->addWidget(exportBtn);
    hbox1->addStretch();
    
    QLabel* label3 = createHintLabel(tr("*Export the current source settings and current account favorites."));
    
    QVBoxLayout* vbox = new QVBoxLayout;
    vbox->addItem(titleHbox);
    vbox->addItem(hbox);
    vbox->addWidget(label1);
    vbox->addWidget(label2);
    vbox->addWidget(horizontalLine);
    vbox->addItem(hbox1);
    vbox->addWidget(label3);
    vbox->addStretch();
    
    this->setLayout(vbox);
    
    QObject::connect(importBtn, &QPushButton::clicked, this, [this]() {
        onImportBtnClicked();
    });
    QObject::connect(exportBtn, &QPushButton::clicked, this, [this]() {
        onExportBtnClicked();
    });
}

ImportExportController::~ImportExportController() {
    
}

void ImportExportController::switchLanguage() {
}

void ImportExportController::onImportBtnClicked() {
    constexpr auto action = ConfigTransferUiAction::ImportClick;
    const std::u8string attemptId = reportConfigTransferUiAction(action);
    const auto startedAt = std::chrono::steady_clock::now();
    auto finish = [&](ConfigTransferResult result,
                      ConfigTransferFailureReason failureReason) {
        const auto durationMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startedAt).count());
        reportConfigTransferResult(
            action, attemptId, result, failureReason, durationMs);
    };

    const SettingTarget target = setting_->activeSettingTarget();
    if (!target.isValid()) {
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::NoActiveConfig);
        QMessageBox::warning(this, tr("Import Config"), tr("Failed to import config."));
        return;
    }

    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Configure File"),
                                                    "~/",
                                                    configFileFilter(true));
    if (fileName.isEmpty()) {
        finish(ConfigTransferResult::Cancelled,
               ConfigTransferFailureReason::FileDialogCancelled);
        return ;
    }

    const QString displayPath = QDir::toNativeSeparators(fileName);
    importPath_->setText(displayPath);
    importPath_->setToolTip(displayPath);

    if (!setting_->isActiveSettingTarget(target)) {
        LogWarn << "import config target changed before validation" << logEnd;
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::ApplyFailed);
        QMessageBox::warning(this, tr("Import Config"), tr("Failed to import config."));
        return;
    }

    const QFileInfo fileInfo(fileName);
    if (!fileInfo.isFile() || fileInfo.size() > kMaxImportFileSize) {
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::InvalidFile);
        QMessageBox::warning(this, tr("Import Config"), tr("Invalid config file."));
        return;
    }
    
    nlohmann::json j;
    const auto path = std::filesystem::path(to_u8(fileName));
    if (!FileSystemOp::loadJson(path, j) || j.is_discarded() || !j.is_object()) {
        LogError << "import config invalid file, path:" << fileName << logEnd;
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::InvalidFile);
        QMessageBox::warning(this, tr("Import Config"), tr("Invalid config file."));
        return;
    }

    nlohmann::json config;
    nlohmann::json favorites;
    if (!parseImportedDocument(
            j, app_->settingConfig()->defaultConfig(), config, favorites)) {
        LogError << "import config unsupported or invalid, path:" << fileName << logEnd;
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::InvalidDocument);
        QMessageBox::warning(this, tr("Import Config"), tr("Invalid config file."));
        return;
    }

    auto ret = QMessageBox::question(
        this,
        tr("Import Config"),
        tr("Importing the configuration file will overwrite the current source settings and current account favorites. Continue?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        finish(ConfigTransferResult::Cancelled,
               ConfigTransferFailureReason::UserCancelled);
        return;
    }

    if (!setting_->isActiveSettingTarget(target)) {
        LogWarn << "import config target changed before apply" << logEnd;
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::ApplyFailed);
        QMessageBox::warning(this, tr("Import Config"), tr("Failed to import config."));
        return;
    }

    if (!setting_->importActiveSettingConfig(target, std::move(config), favorites)) {
        LogError << "import config failed, path:" << fileName << logEnd;
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::ApplyFailed);
        QMessageBox::warning(this, tr("Import Config"), tr("Failed to import config."));
        return;
    }

    LogInfo << "import config success, path:" << fileName << logEnd;
    finish(ConfigTransferResult::Succeeded,
           ConfigTransferFailureReason::None);
    QMessageBox::information(this, tr("Import Config"),
                             tr("Config imported successfully."));
}

void ImportExportController::onExportBtnClicked() {
    constexpr auto action = ConfigTransferUiAction::ExportClick;
    const std::u8string attemptId = reportConfigTransferUiAction(action);
    const auto startedAt = std::chrono::steady_clock::now();
    auto finish = [&](ConfigTransferResult result,
                      ConfigTransferFailureReason failureReason) {
        const auto durationMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startedAt).count());
        reportConfigTransferResult(
            action, attemptId, result, failureReason, durationMs);
    };

    const SettingTarget target = setting_->activeSettingTarget();
    if (!target.isValid()) {
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::NoActiveConfig);
        QMessageBox::warning(this, tr("Export Config"), tr("Failed to export config."));
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Configure File"),
                                                    "~/",
                                                    configFileFilter(false));
    if (fileName.isEmpty()) {
        finish(ConfigTransferResult::Cancelled,
               ConfigTransferFailureReason::FileDialogCancelled);
        return ;
    }
    
    fileName = ensureConfigSuffix(fileName);

    if (!setting_->isActiveSettingTarget(target)) {
        LogWarn << "export config target changed before read" << logEnd;
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::NoActiveConfig);
        QMessageBox::warning(this, tr("Export Config"), tr("Failed to export config."));
        return;
    }

    auto config = setting_->activeSettingConfig(target);
    if (!config.is_object() || config.empty()) {
        LogError << "export config failed, no active config" << logEnd;
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::NoActiveConfig);
        QMessageBox::warning(this, tr("Export Config"), tr("Failed to export config."));
        return;
    }

    auto document = createExportDocument(
        std::move(config), app_->settingConfig()->favorites());

    const auto path = std::filesystem::path(to_u8(fileName));
    if (!FileSystemOp::storeJson(path, document)) {
        LogError << "export config failed, path:" << fileName << logEnd;
        finish(ConfigTransferResult::Failed,
               ConfigTransferFailureReason::WriteFailed);
        QMessageBox::warning(this, tr("Export Config"), tr("Failed to export config."));
        return;
    }

    LogInfo << "export config success, path:" << fileName << logEnd;
    finish(ConfigTransferResult::Succeeded,
           ConfigTransferFailureReason::None);
    QMessageBox::information(this, tr("Export Config"), tr("Config exported successfully."));
}
