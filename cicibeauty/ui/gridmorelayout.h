// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <QList>
#include <QGridLayout>

class QWidget;
class GridMoreLayout : public QLayout {
    Q_OBJECT

public:
    GridMoreLayout(QWidget* parent = nullptr);
    ~GridMoreLayout() override;
    
public:
    void setTroubleMaker(QLayoutItem* t);
    void showTroubleMaker(QWidget* selectedWidget);
    void hideTroubleMaker();
    void setHorizontalSpacing(int n);
    void setVerticalSpacing(int n);

public:
    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    void setGeometry(const QRect &r) override;
    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    
private:
    int doLayout(const QRect &rect, bool testOnly) const;
    
private:
    QList<QLayoutItem*> items_;
    QLayoutItem* troubleMaker_ = nullptr;
    QLayoutItem* selectedItem_ = nullptr;
    bool showTroubleMaker_ = false;
    int horizontalSpacing_ = 2;
    int verticalSpacing_ = 2;
};
