// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <QObject>
#include "nlohmann/json.hpp"
#include "lumaclient.h"

class ISettingConfig : public QObject {
    Q_OBJECT
public:
  virtual ~ISettingConfig() = default;
  virtual bool isReady() const = 0;
  virtual nlohmann::json defaultConfig() const = 0;
  virtual nlohmann::json config(const std::u8string& id) const = 0;
  virtual void refreshConfig(const std::u8string& id) = 0;
  // Returns local acceptance; complete reports the asynchronous backend result.
  virtual bool setConfig(const std::u8string& id, const nlohmann::json& value,
                         std::function<void(bool)> complete = {}) = 0;
  virtual void applyPatch(const std::u8string& id, const nlohmann::json& patch) = 0;
  virtual bool isFavorite(std::u8string_view page, uint64_t effectId) const = 0;
  virtual bool setFavorite(std::u8string_view page, uint64_t effectId, bool favorite) = 0;
  virtual nlohmann::json favorites() const = 0;
  virtual bool replaceFavorites(const nlohmann::json& favorites, std::function<void(bool)> complete = {}) = 0;

signals:
  void ready();
  void invalidated();
  void configAdjusted(const std::u8string& id);
  void favoritesChanged();
};

// UI-thread cache. Storage, normalization and effect execution belong to luma.
class SettingConfig final : public ISettingConfig {
public:
  SettingConfig();
  ~SettingConfig() override;
  bool isReady() const override {
      return ready_;
  }
  nlohmann::json defaultConfig() const override {
      return defaults_;
  }
  nlohmann::json config(const std::u8string& id) const override;
  void refreshConfig(const std::u8string& id) override;
  bool setConfig(const std::u8string& id, const nlohmann::json& value,
                 std::function<void(bool)> complete = {}) override;
  void applyPatch(const std::u8string& id, const nlohmann::json& patch) override;
  bool isFavorite(std::u8string_view page, uint64_t effectId) const override;
  bool setFavorite(std::u8string_view page, uint64_t effectId, bool favorite) override;
  nlohmann::json favorites() const override {
      return favorites_;
  }
  bool replaceFavorites(const nlohmann::json& favorites, std::function<void(bool)> complete = {}) override;

private:
  void requestSnapshot();
  void updateSnapshot(const nlohmann::json& body);
  void applyConfig(const std::u8string& id, const nlohmann::json& value, const nlohmann::json& expected);
  void refreshFavorites();
  bool changeConfig(std::string_view method, const std::u8string& id, nlohmann::json body,
                    nlohmann::json next, std::function<void(bool)> complete = {});
  bool changeFavorites(std::string_view method, nlohmann::json body, nlohmann::json next,
                       std::function<void(bool)> complete = {});
  std::u8string uid_;
  nlohmann::json defaults_ = {{"version", 1}};
  nlohmann::json favorites_ = {{"version", 1}, {"pages", nlohmann::json::object()}};
  std::map<std::u8string, nlohmann::json> configs_;
  LumaRequestScope requests_;
  bool ready_ = false;
};
