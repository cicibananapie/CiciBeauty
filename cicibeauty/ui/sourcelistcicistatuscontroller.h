// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QPointer>
#include <QObject>

class GlobalModel;
class QAbstractItemModel;
class QEvent;
class QListView;
class QMainWindow;
class QModelIndex;
class QWidget;

class SourceListCiciStatusController : public QObject {
public:
    explicit SourceListCiciStatusController(GlobalModel* app);
    ~SourceListCiciStatusController() override;

    bool attach();
    void detach();
    void scheduleRefresh();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refreshRows();
    bool sourceHasCici(const QModelIndex& index) const;
    QWidget* ensureBadge(QWidget* rowWidget) const;
    void removeBadges();

    GlobalModel* app_ = nullptr;
    QPointer<QMainWindow> mainWindow_;
    QPointer<QListView> sourceList_;
    QPointer<QAbstractItemModel> sourceModel_;
    bool refreshPending_ = false;
    bool badgeInsertFailureLogged_ = false;
};
