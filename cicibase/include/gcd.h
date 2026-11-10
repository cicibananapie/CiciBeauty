// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once
#include <stdint.h>

#include <functional>

bool asyncRun(std::function<void()>&& task);
void asyncRunUI(std::function<void()>&& task);
void asyncRunLater(std::function<void()>&& task, uint32_t ms);
void asyncRunUILater(std::function<void()>&& task, uint32_t ms);
