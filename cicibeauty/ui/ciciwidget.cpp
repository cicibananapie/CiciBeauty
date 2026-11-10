// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <assert.h>

#include <QShowEvent>
#include <QScrollArea>
#include <QVBoxLayout>

#include "ciciwidget.h"
#include "nlohmann/json.hpp"

void CiciLogicBase::reset() {}

void CiciLogicBase::json2Me(const nlohmann::json& j) {}

void CiciWidget::showEvent(QShowEvent *event) {
    if (firstShow_) {
        firstShow_ = false;
        onFirstShow();
    }
    
    QWidget::showEvent(event);
}

void CiciWidget::addScrollBar(QLayout* layout) {
    QWidget *container = new QWidget();
    container->setLayout(layout);
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(container);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(scrollArea);
}
