// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#pragma once
#include <random>

class RandomGenerator {
public:
    static int randOnce(int min, int max);
    
public:
    RandomGenerator();
    
    void setSeed(unsigned int s);
    int gen(int min, int max);
    
private:
    std::random_device rd_;
    std::mt19937 gen_;
};
