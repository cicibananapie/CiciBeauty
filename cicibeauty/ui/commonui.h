// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>
#include <vector>

#include <QIcon>
#include <QSize>

const int KSpacing = 20;
const int KTailSpacing = 18;
const int KHeadSpading = 18;

class QString;
class QWidget;
class QLabel;
class QCheckBox;
class QPushButton;
class QRadioButton;
class QSlider;
class QLineEdit;
class QComboBox;
class QTabWidget;
class QLayoutItem;
class GlobalModel;

QLabel* createLabel(const QString& str);
QLabel* createMarqueeLabel(const QString& str);
QLabel* createVideoInputLabel(GlobalModel *app, const QString &fallback);
QLabel* createFixWidthLabel(const QString& str, int w = 50);
QLabel* createFixWidthMarqueeLabel(const QString& str, int w = 50);
QCheckBox* createCheckBox(const QString& str);
QRadioButton* createRadioButton(const QString& str);
QPushButton* createPushButton(const QString& str);
QPushButton* createMarqueeButton(const QString& str);
QSlider* createSlider();
QSlider* createFixWidthSlider(int w = 300);
QLineEdit* createEdit();
QComboBox* createComboBox();
void applyDisabledTabStyle(QTabWidget* tabWidget);
QLayoutItem* createFixedHorizontalSpacer(int width);
QLayoutItem* createExpandingHorizontalSpacer();
QLayoutItem* createExpandingVerticalSpacer();
QWidget* createHSeperator();
QWidget* createVSeperator();
const QIcon& ciciPluginIcon();
void applyCiciWindowIcon(QWidget* widget);

uint32_t uniqueId(const std::vector<uint32_t>& ids);
QString videoInputDisplayText(GlobalModel *app, const QString &fallback);
