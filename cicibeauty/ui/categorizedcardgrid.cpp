// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "categorizedcardgrid.h"

#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTabBar>
#include <QVBoxLayout>

namespace {
constexpr int kGridGap = 12;
}

CategorizedCardGrid::CategorizedCardGrid(QWidget* parent)
    : QWidget(parent),
      tabs_(new QTabBar(this)),
      scrollArea_(new QScrollArea(this)),
      page_(new QWidget),
      grid_(new QGridLayout),
      emptyLabel_(new QLabel(page_)) {
    tabs_->setExpanding(false);
    tabs_->setElideMode(Qt::ElideNone);
    tabs_->setUsesScrollButtons(true);
    tabs_->hide();

    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(kGridGap);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setTextFormat(Qt::PlainText);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->hide();

    auto* content = new QVBoxLayout(page_);
    content->setContentsMargins(0, 0, 0, 0);
    content->addLayout(grid_);
    content->setAlignment(grid_, Qt::AlignLeft);
    content->addWidget(emptyLabel_);
    content->addStretch();

    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scrollArea_->setWidget(page_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs_);
    layout->addWidget(scrollArea_, 1);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0) {
            showCurrentCategory();
            emit categoryActivated(currentCategory());
        }
    });
    scrollArea_->viewport()->installEventFilter(this);
}

void CategorizedCardGrid::setCategories(const QList<Category>& categories) {
    const auto previous = currentCategory();
    const QSignalBlocker blocker(tabs_);
    while (tabs_->count() > 0) {
        tabs_->removeTab(tabs_->count() - 1);
    }

    categories_ = categories;
    for (const auto& category : categories_) {
        tabs_->addTab(category.title);
        adoptCards(category.cards);
    }

    const int index = categoryIndex(previous);
    tabs_->setCurrentIndex(index >= 0 ? index : 0);
    tabs_->setVisible(!categories_.isEmpty());
    if (currentCategory() != previous) {
        showCurrentCategory();
    } else {
        refreshGrid();
    }
}

bool CategorizedCardGrid::setCategoryCards(const QString& id, const QList<QWidget*>& cards) {
    const int index = categoryIndex(id);
    if (index < 0) {
        return false;
    }

    adoptCards(cards);
    categories_[index].cards = cards;
    if (index == tabs_->currentIndex()) {
        refreshGrid();
    }
    return true;
}

QString CategorizedCardGrid::currentCategory() const {
    const int index = tabs_->currentIndex();
    return index >= 0 ? categories_.at(index).id : QString{};
}

bool CategorizedCardGrid::setCurrentCategory(const QString& id) {
    const int index = categoryIndex(id);
    if (index < 0) {
        return false;
    }
    if (index != tabs_->currentIndex()) {
        const QSignalBlocker blocker(tabs_);
        tabs_->setCurrentIndex(index);
        showCurrentCategory();
    }
    return true;
}

bool CategorizedCardGrid::eventFilter(QObject* watched, QEvent* event) {
    if (watched == scrollArea_->viewport() && event->type() == QEvent::Resize && columnCount() != columns_) {
        refreshGrid();
    }
    return QWidget::eventFilter(watched, event);
}

int CategorizedCardGrid::categoryIndex(const QString& id) const {
    for (int index = 0; index < categories_.size(); ++index) {
        if (categories_.at(index).id == id) {
            return index;
        }
    }
    return -1;
}

int CategorizedCardGrid::columnCount() const {
    if (cardWidth_ <= 0) {
        return 1;
    }
    return qMax(1, (scrollArea_->viewport()->width() + kGridGap) / (cardWidth_ + kGridGap));
}

void CategorizedCardGrid::adoptCards(const QList<QWidget*>& cards) {
    for (auto* card : cards) {
        if (card != nullptr && card->parentWidget() != page_) {
            card->setParent(page_);
            card->hide();
        }
    }
}

void CategorizedCardGrid::refreshGrid() {
    const int index = tabs_->currentIndex();
    const auto* category = index >= 0 ? &categories_.at(index) : nullptr;
    const auto cards = category != nullptr ? category->cards : QList<QWidget*>{};
    QSet<QWidget*> visibleCards(cards.begin(), cards.end());

    while (auto* item = grid_->takeAt(0)) {
        if (auto* card = item->widget(); card != nullptr && !visibleCards.contains(card)) {
            card->hide();
        }
        delete item;
    }

    cardWidth_ = 0;
    for (auto* card : cards) {
        if (card != nullptr) {
            cardWidth_ = qMax(cardWidth_, card->sizeHint().width());
        }
    }
    columns_ = columnCount();

    int count = 0;
    for (auto* card : cards) {
        if (card != nullptr && visibleCards.remove(card)) {
            grid_->addWidget(card, count / columns_, count % columns_);
            card->show();
            ++count;
        }
    }

    emptyLabel_->setText(category != nullptr ? category->emptyText : QString{});
    emptyLabel_->setVisible(count == 0 && !emptyLabel_->text().isEmpty());
}

void CategorizedCardGrid::showCurrentCategory() {
    refreshGrid();
    scrollArea_->verticalScrollBar()->setValue(0);
}
