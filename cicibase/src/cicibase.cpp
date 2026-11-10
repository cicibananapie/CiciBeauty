// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>
#include <thread>

#include "cicibase.h"
#include "ciciconfigure.h"
#include "gcd.h"

std::thread::id* pMainTID = nullptr;

bool initCiciGCDEnviroment();

bool initCiciBaseImp() {
    pMainTID = new std::thread::id();
    *pMainTID = std::this_thread::get_id();
    return initCiciGCDEnviroment();
}

bool initCiciBase() {
	static const bool initialized = initCiciBaseImp();
	return initialized;
}

bool isOfficialBuild() {
#ifdef OFFICIAL_BUILD
    return true;
#else
    return false;
#endif
}

const std::thread::id& mainThreadId() {
    return *pMainTID;
}
