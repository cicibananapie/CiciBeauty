// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <assert.h>
#include <vector>

#include <QTabWidget>

#include "commonui.h"

template <typename SolutionControllerT>
uint32_t uniqueSolutionId(QTabWidget* tabWidget) {
    assert(tabWidget != nullptr);

    std::vector<uint32_t> ids;
    for (int i = 0; i < tabWidget->count() - 1; i++) {
        auto sln = qobject_cast<SolutionControllerT*>(tabWidget->widget(i));
        if (sln != nullptr) {
            ids.push_back(sln->id());
        }
    }

    return uniqueId(ids);
}
