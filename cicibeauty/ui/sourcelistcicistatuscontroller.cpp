// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "sourcelistcicistatuscontroller.h"

#include <cassert>

#include <obs-frontend-api.h>
#include <obs.h>

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QBoxLayout>
#include <QByteArray>
#include <QEvent>
#include <QLabel>
#include <QListView>
#include <QMainWindow>
#include <QMetaObject>
#include <QModelIndex>
#include <QObject>
#include <QPointer>
#include <QSizePolicy>
#include <QVariant>
#include <QWidget>

#include "../modelapp.h"
#include "../qstringconv.h"
#include "commonui.h"
#include "log.h"

namespace {

constexpr const char* kObsSourceListName = "sources";
constexpr const char* kVisibilityClassName = "indicator-visibility";
constexpr const char* kLegacyVisibilityProperty = "visibilityCheckBox";
constexpr const char* kBadgeObjectName = "ciciSourceStatusBadge";

QMainWindow* obsMainWindow() {
    auto* widget = reinterpret_cast<QWidget*>(
        obs_frontend_get_main_window());
    return qobject_cast<QMainWindow*>(widget);
}

bool isVisibilityButton(QAbstractButton* button) {
    if (button == nullptr) {
        return false;
    }

    // OBS 30.x identifies this control with a boolean property. OBS 31+
    // moved the marker into its space-separated "class" property.
    if (button->property(kLegacyVisibilityProperty).toBool()) {
        return true;
    }

    return button->property("class")
        .toString()
        .contains(QString::fromLatin1(kVisibilityClassName));
}

}

SourceListCiciStatusController::SourceListCiciStatusController(
    GlobalModel* app)
    : app_(app) {
    assert(app_ != nullptr);
}

SourceListCiciStatusController::~SourceListCiciStatusController() {
    detach();
}

bool SourceListCiciStatusController::attach() {
    if (sourceList_ != nullptr) {
        return true;
    }

    mainWindow_ = obsMainWindow();
    if (mainWindow_ == nullptr) {
        LogWarn << "SourceListCiciStatusController.attach: "
                   "OBS main window is null"
                << logEnd;
        return false;
    }

    sourceList_ = mainWindow_->findChild<QListView*>(
        QString::fromLatin1(kObsSourceListName));
    if (sourceList_ == nullptr) {
        LogWarn << "SourceListCiciStatusController.attach: "
                   "cannot find OBS source list"
                << logEnd;
        mainWindow_ = nullptr;
        return false;
    }

    sourceModel_ = sourceList_->model();
    if (sourceModel_ == nullptr) {
        LogWarn << "SourceListCiciStatusController.attach: "
                   "OBS source list has no model"
                << logEnd;
        sourceList_ = nullptr;
        mainWindow_ = nullptr;
        return false;
    }

    QObject::connect(
        sourceModel_,
        &QAbstractItemModel::modelReset,
        this,
        [this]() {
            scheduleRefresh();
        });
    QObject::connect(
        sourceModel_,
        &QAbstractItemModel::rowsInserted,
        this,
        [this](const QModelIndex&, int, int) {
            scheduleRefresh();
        });
    QObject::connect(
        sourceModel_,
        &QAbstractItemModel::rowsRemoved,
        this,
        [this](const QModelIndex&, int, int) {
            scheduleRefresh();
        });
    QObject::connect(
        sourceModel_,
        &QAbstractItemModel::rowsMoved,
        this,
        [this](const QModelIndex&, int, int,
               const QModelIndex&, int) {
            scheduleRefresh();
        });

    sourceList_->installEventFilter(this);
    sourceList_->viewport()->installEventFilter(this);
    scheduleRefresh();

    LogInfo << "SourceListCiciStatusController.attach: "
               "attached to OBS source list"
            << logEnd;
    return true;
}

void SourceListCiciStatusController::detach() {
    removeBadges();

    if (sourceList_ != nullptr) {
        sourceList_->removeEventFilter(this);
        sourceList_->viewport()->removeEventFilter(this);
    }
    if (sourceModel_ != nullptr) {
        QObject::disconnect(sourceModel_, nullptr, this, nullptr);
    }

    refreshPending_ = false;
    badgeInsertFailureLogged_ = false;
    sourceModel_ = nullptr;
    sourceList_ = nullptr;
    mainWindow_ = nullptr;
}

void SourceListCiciStatusController::scheduleRefresh() {
    if (sourceList_ == nullptr || refreshPending_) {
        return;
    }

    refreshPending_ = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            refreshPending_ = false;
            refreshRows();
        },
        Qt::QueuedConnection);
}

bool SourceListCiciStatusController::eventFilter(
    QObject* watched,
    QEvent* event) {
    if (event != nullptr) {
        if (watched == sourceList_
            && (event->type() == QEvent::Show
                || event->type() == QEvent::StyleChange
                || event->type() == QEvent::LanguageChange)) {
            scheduleRefresh();
        }
        else if (sourceList_ != nullptr
                 && watched == sourceList_->viewport()
                 && event->type() == QEvent::ChildAdded) {
            scheduleRefresh();
        }
    }

    return QObject::eventFilter(watched, event);
}

void SourceListCiciStatusController::refreshRows() {
    if (sourceList_ == nullptr || sourceModel_ == nullptr) {
        return;
    }

    const int rowCount = sourceModel_->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const QModelIndex index = sourceModel_->index(row, 0);
        QWidget* rowWidget = sourceList_->indexWidget(index);
        if (rowWidget == nullptr) {
            continue;
        }

        QWidget* badge = rowWidget->findChild<QWidget*>(
            QString::fromLatin1(kBadgeObjectName),
            Qt::FindDirectChildrenOnly);
        const bool hasCici = sourceHasCici(index);
        if (!hasCici) {
            if (badge != nullptr) {
                badge->hide();
            }
            continue;
        }

        if (badge == nullptr) {
            badge = ensureBadge(rowWidget);
        }
        if (badge != nullptr) {
            badge->setToolTip(
                QObject::tr("Cici Beauty is applied to this source"));
            badge->show();
        }
        else if (!badgeInsertFailureLogged_) {
            badgeInsertFailureLogged_ = true;
            LogWarn << "SourceListCiciStatusController.refreshRows: "
                       "unsupported OBS source row layout"
                    << logEnd;
        }
    }
}

bool SourceListCiciStatusController::sourceHasCici(
    const QModelIndex& index) const {
    if (!index.isValid()) {
        return false;
    }

    const QString sourceName =
        index.data(Qt::AccessibleTextRole).toString();
    if (sourceName.isEmpty()) {
        return false;
    }

    const QByteArray sourceNameUtf8 = sourceName.toUtf8();
    obs_source_t* source = obs_get_source_by_name(sourceNameUtf8.constData());
    if (source == nullptr) {
        return false;
    }

    const char* sourceUuid = obs_source_get_uuid(source);
    const bool hasCici = sourceUuid != nullptr
        && sourceUuid[0] != '\0'
        && app_->hasCiciFilterOnSource(to_u8(sourceUuid));
    obs_source_release(source);
    return hasCici;
}

QWidget* SourceListCiciStatusController::ensureBadge(
    QWidget* rowWidget) const {
    auto* layout = qobject_cast<QBoxLayout*>(rowWidget->layout());
    if (layout == nullptr) {
        return nullptr;
    }

    QAbstractButton* visibilityButton = nullptr;
    const auto buttons = rowWidget->findChildren<QAbstractButton*>(
        QString(),
        Qt::FindDirectChildrenOnly);
    for (QAbstractButton* button : buttons) {
        if (isVisibilityButton(button)) {
            visibilityButton = button;
            break;
        }
    }
    if (visibilityButton == nullptr) {
        return nullptr;
    }

    const int insertIndex = layout->indexOf(visibilityButton);
    if (insertIndex < 0) {
        return nullptr;
    }

    constexpr int kBadgeIconSize = 13;

    auto* badge = new QWidget(rowWidget);
    badge->setObjectName(QString::fromLatin1(kBadgeObjectName));
    badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    badge->setFixedHeight(19);
    badge->setMinimumWidth(50);
    badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    badge->setAccessibleName(QStringLiteral("Cici"));
    badge->setToolTip(
        QObject::tr("Cici Beauty is applied to this source"));
    badge->setStyleSheet(QStringLiteral(
        "QWidget#ciciSourceStatusBadge {"
        "background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,"
        "                            stop: 0 #334A3D,"
        "                            stop: 0.55 #30463A,"
        "                            stop: 1 #2D4136);"
        "border: 1px solid #395345;"
        "border-radius: 8px;"
        "margin-right: 3px;"
        "}"));

    auto* icon = new QLabel(badge);
    icon->setPixmap(ciciPluginIcon().pixmap(kBadgeIconSize,
                                            kBadgeIconSize));
    icon->setFixedSize(kBadgeIconSize, kBadgeIconSize);
    icon->setAlignment(Qt::AlignCenter);

    auto* text = new QLabel(QStringLiteral("Cici"), badge);
    text->setAlignment(Qt::AlignCenter);
    text->setStyleSheet(QStringLiteral(
        "color: #82BD96;"
        "background: transparent;"
        "border: none;"
        "font-weight: 500;"));

    auto* badgeLayout = new QHBoxLayout(badge);
    badgeLayout->setContentsMargins(5, 0, 6, 0);
    badgeLayout->setSpacing(4);
    badgeLayout->addWidget(icon);
    badgeLayout->addWidget(text);

    layout->insertWidget(insertIndex, badge, 0, Qt::AlignVCenter);
    return badge;
}

void SourceListCiciStatusController::removeBadges() {
    if (sourceList_ == nullptr) {
        return;
    }

    const auto badges = sourceList_->findChildren<QWidget*>(
        QString::fromLatin1(kBadgeObjectName));
    for (QWidget* badge : badges) {
        delete badge;
    }
}
