// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once

#include <stdint.h>
#include <thread>
#include <assert.h>
/**

    make sure to call initCiciBase in UI thread(main thread) as earlier as possible!!!
 */
bool initCiciBase();
bool isOfficialBuild();

const std::thread::id& mainThreadId();

inline void makesureMainThread() {assert(std::this_thread::get_id() == mainThreadId());}

inline bool isMainThread() {return std::this_thread::get_id() == mainThreadId();}
