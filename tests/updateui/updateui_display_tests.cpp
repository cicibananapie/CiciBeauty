// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QScrollArea>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>

#include "updatedescriptionpages.h"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QPixmap makeImage(const QColor& color) {
    QPixmap image(320, 180);
    image.fill(color);
    return image;
}

QScrollArea* textArea(QWidget* page) {
    return page->findChild<QScrollArea*>(UpdateDescriptionObjectName::TextArea);
}

std::vector<QLabel*> textLabels(QWidget* page) {
    auto* area = textArea(page);
    require(area != nullptr, "description text area is missing");
    require(area->widget() != nullptr, "description text content is missing");
    require(area->widget()->layout() != nullptr, "description text layout is missing");

    std::vector<QLabel*> labels;
    auto* layout = area->widget()->layout();
    for (int index = 0; index < layout->count(); ++index) {
        auto* label = qobject_cast<QLabel*>(layout->itemAt(index)->widget());
        if (label != nullptr
            && label->objectName() == UpdateDescriptionObjectName::TextLabel) {
            labels.push_back(label);
        }
    }
    return labels;
}

void requireTextLabelStyle(QLabel* label) {
    require(label != nullptr, "description label is null");
    require(label->wordWrap(), "description label does not wrap");
    require((label->alignment() & Qt::AlignHorizontal_Mask) == Qt::AlignLeft,
            "description label is not left aligned");
    require((label->alignment() & Qt::AlignVertical_Mask) == Qt::AlignTop,
            "description label is not top aligned");
}

void requirePageTexts(QWidget* page, const std::vector<QString>& expected) {
    const auto labels = textLabels(page);
    require(labels.size() == expected.size(), "unexpected description label count");
    for (size_t index = 0; index < expected.size(); ++index) {
        require(labels[index]->text() == expected[index], "description order or text mismatch");
        requireTextLabelStyle(labels[index]);
    }
}

void testAllTextDescriptionsShareOnePage() {
    QStackedWidget stack;
    const QString longText = QStringLiteral(
        "This is a deliberately long update description that must wrap across multiple "
        "visual lines when the available width is constrained by the update dialog.");
    populateUpdateDescriptionPages(&stack, {
        {QStringLiteral("First description"), {}},
        {longText, {}},
        {QStringLiteral("Third description"), {}},
    });

    require(stack.count() == 1, "consecutive text descriptions were not aggregated");
    requirePageTexts(stack.widget(0), {
        QStringLiteral("First description"),
        longText,
        QStringLiteral("Third description"),
    });

    auto* area = textArea(stack.widget(0));
    require(area->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
            "text page allows horizontal scrolling");
    require(area->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
            "text page does not enable vertical scrolling as needed");

    const auto labels = textLabels(stack.widget(0));
    require(labels[1]->hasHeightForWidth(), "wrapped label has no height-for-width support");
    require(labels[1]->heightForWidth(160) > QFontMetrics(labels[1]->font()).lineSpacing(),
            "long description does not require multiple lines");
}

void testMixedDescriptionsPreservePageOrderAndLayout() {
    QStackedWidget stack;
    stack.resize(560, 300);
    populateUpdateDescriptionPages(&stack, {
        {QStringLiteral("Text A"), {}},
        {QStringLiteral("Image B"), makeImage(Qt::red)},
        {QStringLiteral("Text C"), {}},
        {QStringLiteral("Text D"), {}},
        {QStringLiteral("Image E"), makeImage(Qt::blue)},
        {QStringLiteral("Text F"), {}},
    });

    require(stack.count() == 5, "mixed descriptions produced the wrong page count");
    requirePageTexts(stack.widget(0), {QStringLiteral("Text A")});
    requirePageTexts(stack.widget(1), {QStringLiteral("Image B")});
    requirePageTexts(stack.widget(2), {QStringLiteral("Text C"), QStringLiteral("Text D")});
    requirePageTexts(stack.widget(3), {QStringLiteral("Image E")});
    requirePageTexts(stack.widget(4), {QStringLiteral("Text F")});

    require(stack.widget(0)->findChild<QLabel*>(UpdateDescriptionObjectName::ImageLabel) == nullptr,
            "text page unexpectedly contains an image label");

    stack.setCurrentIndex(1);
    stack.show();
    QApplication::processEvents();
    auto* imageLabel = stack.widget(1)->findChild<QLabel*>(
        UpdateDescriptionObjectName::ImageLabel);
    auto* area = textArea(stack.widget(1));
    require(imageLabel != nullptr, "image page is missing its image label");
    require(area != nullptr, "image page is missing its text area");
    require(imageLabel->geometry().bottom() < area->geometry().top(),
            "image is not laid out above its description");
    require(imageLabel->height() == 192, "image area height is not stable");
    require(imageLabel->pixmap().size() == imageLabel->contentsRect().size(),
            "image does not fill the complete image area");
}

void testUnavailableImagesMergeWithAdjacentText() {
    QStackedWidget stack;
    populateUpdateDescriptionPages(&stack, {
        {QStringLiteral("No image A"), {}},
        {QStringLiteral("Failed image B"), {}},
        {QStringLiteral("Timed out image C"), {}},
        {QStringLiteral("Ready image D"), makeImage(Qt::green)},
    });

    require(stack.count() == 2, "unavailable images were not merged into the text page");
    requirePageTexts(stack.widget(0), {
        QStringLiteral("No image A"),
        QStringLiteral("Failed image B"),
        QStringLiteral("Timed out image C"),
    });
    requirePageTexts(stack.widget(1), {QStringLiteral("Ready image D")});
    require(stack.widget(0)->findChild<QLabel*>(UpdateDescriptionObjectName::ImageLabel) == nullptr,
            "unavailable image left an empty image area");
}

void testCarouselStartsOnlyForMultiplePages() {
    QStackedWidget singlePageStack;
    populateUpdateDescriptionPages(&singlePageStack, {
        {QStringLiteral("Only text"), {}},
        {QStringLiteral("Still the same page"), {}},
    });
    QTimer* singlePageTimer = startUpdateDescriptionCarousel(
        &singlePageStack, &singlePageStack, 20);
    require(singlePageTimer == nullptr, "single page unexpectedly started a carousel timer");

    QStackedWidget multiplePageStack;
    populateUpdateDescriptionPages(&multiplePageStack, {
        {QStringLiteral("Text page"), {}},
        {QStringLiteral("Image page"), makeImage(Qt::yellow)},
    });
    QTimer* timer = startUpdateDescriptionCarousel(
        &multiplePageStack, &multiplePageStack, 20);
    require(timer != nullptr, "multiple pages did not create a carousel timer");
    require(timer->isActive(), "carousel timer is not active");
    require(timer->interval() == 20, "carousel timer interval is incorrect");

    QElapsedTimer elapsed;
    elapsed.start();
    while (multiplePageStack.currentIndex() == 0 && elapsed.elapsed() < 500) {
        QApplication::processEvents();
        QThread::msleep(2);
    }
    require(multiplePageStack.currentIndex() == 1, "carousel did not advance to the next page");
}

} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication app(argc, argv);

    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"all text descriptions share one page", testAllTextDescriptionsShareOnePage},
        {"mixed descriptions preserve page order and layout", testMixedDescriptionsPreservePageOrderAndLayout},
        {"unavailable images merge with adjacent text", testUnavailableImagesMergeWithAdjacentText},
        {"carousel starts only for multiple pages", testCarouselStartsOnlyForMultiplePages},
    };

    int failed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    }

    std::cout << tests.size() - failed << "/" << tests.size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
