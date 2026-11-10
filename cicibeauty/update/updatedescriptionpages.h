// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <vector>

#include <QPixmap>
#include <QString>

class QObject;
class QStackedWidget;
class QTimer;

struct UpdateDescriptionDisplayItem {
    QString text;
    QPixmap image;
};

namespace UpdateDescriptionObjectName {
inline constexpr char TextLabel[] = "updateDescriptionTextLabel";
inline constexpr char TextArea[] = "updateDescriptionTextArea";
inline constexpr char ImageLabel[] = "updateDescriptionImageLabel";
}

void populateUpdateDescriptionPages(
    QStackedWidget* stack,
    const std::vector<UpdateDescriptionDisplayItem>& descriptions);

QTimer* startUpdateDescriptionCarousel(QStackedWidget* stack,
                                       QObject* parent,
                                       int intervalMs);
