// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#include "i18nsupport.h"

#include <memory>

#include <QCoreApplication>
#include <QTranslator>

#include "nlohmann/json.hpp"
#include "qstringconv.h"

namespace {

QString currentLocaleNameStorage = QStringLiteral("en_US");
std::unique_ptr<QTranslator> translatorStorage;

QString translationResourcePath(const QString& localeName) {
    return QStringLiteral(":/cicibeauty/translations/cicibeauty_%1.qm").arg(localeName);
}

void removeInstalledTranslator() {
    if (translatorStorage != nullptr && QCoreApplication::instance() != nullptr) {
        QCoreApplication::removeTranslator(translatorStorage.get());
    }

    translatorStorage.reset();
}

bool installTranslator(const QString& localeName) {
    if (QCoreApplication::instance() == nullptr) {
        return false;
    }

    removeInstalledTranslator();

    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(translationResourcePath(localeName))) {
        return false;
    }

    if (!QCoreApplication::installTranslator(translator.get())) {
        return false;
    }

    translatorStorage = std::move(translator);
    return true;
}

}

namespace I18nSupport {

void setCurrentLocaleName(const QString& localeName) {
    const QString normalizedLocaleName = normalizeLocaleName(localeName);
    if (currentLocaleNameStorage == normalizedLocaleName && translatorStorage != nullptr) {
        return;
    }

    currentLocaleNameStorage = normalizedLocaleName;
    installTranslator(normalizedLocaleName);
}

QString currentLocaleName() {
    return currentLocaleNameStorage;
}

QString normalizeLocaleName(const QString& localeName) {
    QString name = localeName.trimmed();
    name.replace(QLatin1Char('-'), QLatin1Char('_'));

    if (name.compare(QStringLiteral("zh_TW"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("zh_HK"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("zh_MO"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("zh_Hant"), Qt::CaseInsensitive) == 0
        || name.startsWith(QStringLiteral("zh_Hant_"), Qt::CaseInsensitive)) {
        return QStringLiteral("zh_TW");
    }

    return QStringLiteral("en_US");
}

QString localizedString(const nlohmann::json& i18nObject) {
    if (!i18nObject.is_object()) {
        return QString();
    }

    const auto it = i18nObject.find(to_char(to_u8(currentLocaleName())));
    if (it != i18nObject.end() && it->is_string()) {
        return from_json(*it);
    }

    return QString();
}

QString localizedName(const nlohmann::json& item) {
    if (!item.is_object()) {
        return QString();
    }

    return localizedString(item.value("name_i18n", nlohmann::json()));
}

}
