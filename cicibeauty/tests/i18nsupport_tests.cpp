// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "i18nsupport.h"

#include <iostream>
#include <stdexcept>
#include <string>

#include <QCoreApplication>

#include "nlohmann/json.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireLocale(const char* input, const char* expected) {
    const QString actual = I18nSupport::normalizeLocaleName(QString::fromUtf8(input));
    require(actual == QString::fromUtf8(expected), "locale normalized incorrectly");
}

void testSimplifiedChineseFallsBackToEnglish() {
    requireLocale("zh_CN", "en_US");
    requireLocale("zh-CN", "en_US");
    requireLocale("zh_Hans", "en_US");
    requireLocale("zh-Hans-CN", "en_US");
    requireLocale("zh", "en_US");
}

void testSupportedLocalesRemainAvailable() {
    requireLocale("en_US", "en_US");
    requireLocale("en-US", "en_US");
    requireLocale("en_GB", "en_US");
    requireLocale("zh_TW", "zh_TW");
    requireLocale("zh-TW", "zh_TW");
    requireLocale("zh_Hant", "zh_TW");
    requireLocale("zh-Hant-HK", "zh_TW");
    requireLocale("zh_HK", "zh_TW");
    requireLocale("zh_MO", "zh_TW");
}

void testCatalogUsesNormalizedLocale() {
    const nlohmann::json item = {
        {"name_i18n", {
            {"zh_CN", "simplified"},
            {"zh_TW", "traditional"},
            {"en_US", "english"},
        }},
    };

    I18nSupport::setCurrentLocaleName(QStringLiteral("zh_CN"));
    require(I18nSupport::currentLocaleName() == QStringLiteral("en_US"),
            "simplified Chinese remained active");
    require(I18nSupport::localizedName(item) == QStringLiteral("english"),
            "simplified Chinese did not select the English catalog name");

    I18nSupport::setCurrentLocaleName(QStringLiteral("zh_TW"));
    require(I18nSupport::localizedName(item) == QStringLiteral("traditional"),
            "traditional Chinese catalog name was disabled");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testSimplifiedChineseFallsBackToEnglish();
        testSupportedLocalesRemainAvailable();
        testCatalogUsesNormalizedLocale();
    }
    catch (const std::exception& error) {
        std::cerr << "i18nsupport_tests: " << error.what() << '\n';
        return 1;
    }

    std::cout << "i18nsupport_tests: all tests passed\n";
    return 0;
}
