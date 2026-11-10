// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: MIT
// See LICENSES/MIT.txt for the full license text.

#include "cicilocale.h"

#import <Foundation/Foundation.h>

#include "misc.h"

namespace CiciLocale {

std::u8string systemLocale() {
    @autoreleasepool {
        NSArray<NSString*>* languages = [NSLocale preferredLanguages];
        if (languages.count == 0 || languages[0].length == 0) {
            return kEnglishUS;
        }

        return normalize(to_u8([languages[0] UTF8String]));
    }
}

}
