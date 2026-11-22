// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QGridLayout;
class QLabel;
class QScrollArea;
class QTabBar;

class CategorizedCardGrid : public QWidget {
    Q_OBJECT

public:
    struct Category {
        QString id; // Non-empty and unique within the grid.
        QString title;
        QString emptyText;
        QList<QWidget*> cards;
    };

    explicit CategorizedCardGrid(QWidget* parent = nullptr);

    // The grid owns supplied cards. Removing a card from a category only hides
    // it when it is not in the active category; it does not destroy the card.
    void setCategories(const QList<Category>& categories);
    bool setCategoryCards(const QString& id, const QList<QWidget*>& cards);

    QString currentCategory() const;
    bool setCurrentCategory(const QString& id);

signals:
    void categoryActivated(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    int categoryIndex(const QString& id) const;
    int columnCount() const;
    void adoptCards(const QList<QWidget*>& cards);
    void refreshGrid();
    void showCurrentCategory();

    QTabBar* tabs_;
    QScrollArea* scrollArea_;
    QWidget* page_;
    QGridLayout* grid_;
    QLabel* emptyLabel_;
    QList<Category> categories_;
    int columns_ = 0;
};
