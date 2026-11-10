// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include <assert.h>

#include "randwrapper.h"

int RandomGenerator::randOnce(int min, int max) {
    RandomGenerator r;
    return r.gen(min, max);
}

RandomGenerator::RandomGenerator():gen_(rd_()) {
    
}

void RandomGenerator::setSeed(unsigned int s) {
    gen_.seed(s);
}

int RandomGenerator::gen(int min, int max) {
    std::uniform_int_distribution<int> dis(min, max);
    return dis(gen_);
}
