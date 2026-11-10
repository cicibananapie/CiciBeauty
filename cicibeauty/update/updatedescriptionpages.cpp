// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "updatedescriptionpages.h"

#include <assert.h>

#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kUpdateImageHeight = 192;
constexpr int kDescriptionSpacing = 6;

class ScaledPixmapLabel final : public QLabel {
public:
    explicit ScaledPixmapLabel(QWidget* parent = nullptr)
        : QLabel(parent) {
        setObjectName(UpdateDescriptionObjectName::ImageLabel);
        setAlignment(Qt::AlignCenter);
        setFixedHeight(kUpdateImageHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setSourcePixmap(const QPixmap& pixmap) {
        sourcePixmap_ = pixmap;
        updateScaledPixmap();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        updateScaledPixmap();
    }

private:
    void updateScaledPixmap() {
        if (sourcePixmap_.isNull() || contentsRect().isEmpty()) {
            QLabel::clear();
            return;
        }

        QLabel::setPixmap(sourcePixmap_.scaled(contentsRect().size(),
                                               Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation));
    }

private:
    QPixmap sourcePixmap_;
};

QLabel* createDescriptionLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(UpdateDescriptionObjectName::TextLabel);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sizePolicy.setHeightForWidth(true);
    label->setSizePolicy(sizePolicy);
    label->setMinimumWidth(0);
    return label;
}

QScrollArea* createDescriptionTextArea(const std::vector<QString>& texts, QWidget* parent) {
    auto* scrollArea = new QScrollArea(parent);
    scrollArea->setObjectName(UpdateDescriptionObjectName::TextArea);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kDescriptionSpacing);
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    for (const auto& text : texts) {
        layout->addWidget(createDescriptionLabel(text, content));
    }
    layout->addStretch();

    scrollArea->setWidget(content);
    return scrollArea;
}

QWidget* createTextPage(const std::vector<QString>& texts, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(createDescriptionTextArea(texts, page));
    return page;
}

QWidget* createImageTextPage(const QString& text, const QPixmap& image, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kDescriptionSpacing);

    auto* imageLabel = new ScaledPixmapLabel(page);
    imageLabel->setSourcePixmap(image);
    layout->addWidget(imageLabel);
    layout->addWidget(createDescriptionTextArea({text}, page), 1);
    return page;
}

} // namespace

void populateUpdateDescriptionPages(
    QStackedWidget* stack,
    const std::vector<UpdateDescriptionDisplayItem>& descriptions) {
    assert(stack != nullptr);
    assert(stack->count() == 0);

    std::vector<QString> pendingTexts;
    auto flushTextPage = [stack, &pendingTexts]() {
        if (pendingTexts.empty()) {
            return;
        }
        stack->addWidget(createTextPage(pendingTexts, stack));
        pendingTexts.clear();
    };

    for (const auto& description : descriptions) {
        if (description.image.isNull()) {
            pendingTexts.push_back(description.text);
            continue;
        }

        flushTextPage();
        stack->addWidget(createImageTextPage(description.text, description.image, stack));
    }
    flushTextPage();

    if (stack->count() == 0) {
        stack->addWidget(createTextPage({}, stack));
    }
    stack->setCurrentIndex(0);
}

QTimer* startUpdateDescriptionCarousel(QStackedWidget* stack,
                                       QObject* parent,
                                       int intervalMs) {
    assert(stack != nullptr);
    assert(parent != nullptr);
    assert(intervalMs > 0);

    stack->setCurrentIndex(0);
    if (stack->count() <= 1) {
        return nullptr;
    }

    auto* timer = new QTimer(parent);
    timer->setInterval(intervalMs);
    QObject::connect(timer, &QTimer::timeout, stack, [stack]() {
        assert(stack->count() > 1);
        stack->setCurrentIndex((stack->currentIndex() + 1) % stack->count());
    });
    timer->start();
    return timer;
}
