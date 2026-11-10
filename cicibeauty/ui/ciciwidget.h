// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once
#include <stdint.h>

#include <QWidget>

#include "nlohmann/json_fwd.hpp"

class QLayout;
class CiciLogicBase {
public:
    virtual void setId(uint32_t id) {id_ = id;}
    uint32_t id() const {return id_;}
    
public:
    virtual void reset();
    virtual void json2Me(const nlohmann::json& j);
    
private:
    uint32_t id_ = static_cast<uint32_t>(-1);
};

class CiciWidget: public QWidget, public CiciLogicBase {
public:
    CiciWidget() {}
    CiciWidget(QWidget* parent):QWidget(parent) {}
    
public:
    void addScrollBar(QLayout* layout);
    
protected:
    void showEvent(QShowEvent *event) override;
    
private:
    virtual void onFirstShow() {};
    
private:
    bool firstShow_ = true;
};
