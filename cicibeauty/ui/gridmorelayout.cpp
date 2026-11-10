// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>

#include "log.h"
#include "gridmorelayout.h"

GridMoreLayout::GridMoreLayout(QWidget* parent/* = nullptr*/) {
    
}

GridMoreLayout::~GridMoreLayout() {
    QLayoutItem *item;
    while ((item = takeAt(0))) {
        delete item;
    }
}

void GridMoreLayout::setTroubleMaker(QLayoutItem* t) {
    troubleMaker_ = t;
}

void GridMoreLayout::showTroubleMaker(QWidget* selectedWidget) {
    showTroubleMaker_ = true;
    for (auto i = 0; i <items_.count(); i++) {
        auto item = items_.at(i);
        if (item->widget() == selectedWidget) {
            selectedItem_ = item;
            update();
            break;
        }
    }
}

void GridMoreLayout::hideTroubleMaker() {
    showTroubleMaker_ = false;
    selectedItem_ = nullptr;
    update();
}

void GridMoreLayout::setHorizontalSpacing(int n) {
    assert(n >= 0);
    horizontalSpacing_ = n;
}

void GridMoreLayout::setVerticalSpacing(int n) {
    assert(n >= 0);
    verticalSpacing_ = n;
}

void GridMoreLayout::addItem(QLayoutItem *item) {
    items_.append(item);
}

int GridMoreLayout::count() const {
    return static_cast<int>(items_.size());
}

QLayoutItem *GridMoreLayout::itemAt(int index) const {
    if (index >= 0 && index < items_.size()) {
        return items_.at(index);
    }
    
    return nullptr;
}

QLayoutItem *GridMoreLayout::takeAt(int index) {
    if (index >= 0 && index < items_.size()) {
        return items_.takeAt(index);  // Remove from the list and return it.
    }
    
    return nullptr;
}

void GridMoreLayout::setGeometry(const QRect &r) {
    doLayout(r, false);
}

QSize GridMoreLayout::sizeHint() const {
    return QSize(100, doLayout(geometry(), true));
}

int GridMoreLayout::heightForWidth(int width) const {
    return doLayout(QRect(0, 0, width, 0), true);
}

int GridMoreLayout::doLayout(const QRect &r, bool testOnly) const {
    QRect rc;
    rc.setX(r.x());
    rc.setY(r.y());
    int maxW = r.right();
    int maxRowHeight = 0;
    int numOfRow = 0;   //number of items that can be contained in this row
    int y = r.y();
    bool foundSelected = false;
    for (auto i = 0; i < items_.size(); i++) {
        auto item = items_.at(i);
        if (item == troubleMaker_) {
            continue;
        }
        
        if (rc.x() + item->sizeHint().width() <= maxW
            || numOfRow == 0) {
            rc.setSize(item->sizeHint());
            if (!testOnly) {
                item->setGeometry(rc);
            }
            maxRowHeight = (rc.height() > maxRowHeight) ? rc.height() : maxRowHeight;
            rc.setX(rc.right() + horizontalSpacing_);   //update X
            numOfRow++;
            if (showTroubleMaker_ && selectedItem_ == item) {
                foundSelected = true;
            }
        }
        else {  //return!!!
            y += maxRowHeight + verticalSpacing_;
            maxRowHeight = 0;
            if (foundSelected) {
                QRect rc;
                rc.setX(r.x());
                rc.setY(y);
                rc.setWidth(r.width());
                rc.setHeight(troubleMaker_->sizeHint().height());
                if (!testOnly) {
                    troubleMaker_->setGeometry(rc);
                }
                foundSelected = false;
                y += verticalSpacing_ + rc.height();
            }
            numOfRow = 0;
            rc.setX(r.x());
            rc.setY(y);  //update Y
            i--;    //back
        }
    }
    
    y += maxRowHeight + verticalSpacing_;
    if (foundSelected) {
        QRect rc;
        rc.setX(r.x());
        rc.setY(y);
        rc.setWidth(r.width());
        rc.setHeight(troubleMaker_->sizeHint().height());
        if (!testOnly) {
            troubleMaker_->setGeometry(rc);
        }
        foundSelected = false;
        y += verticalSpacing_ + rc.height();
    }
    
    if (!showTroubleMaker_ && troubleMaker_ != nullptr && !testOnly) {
        troubleMaker_->setGeometry(QRect(0, 0, 0, 0));
    }
    
    return y - r.y();
}
