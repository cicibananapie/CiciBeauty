// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once
#include <map>
#include <string>
#include <string_view>
#include "nlohmann/json.hpp"

class ISettingConfig {
public:
  virtual ~ISettingConfig() = default;
  virtual void load() = 0;
  virtual nlohmann::json defaultConfig() const = 0;
  virtual nlohmann::json config(const std::u8string& id) const = 0;
  // On success, returns the backend's normalized configuration in value.
  virtual bool setConfig(const std::u8string& id, nlohmann::json& value) = 0;
  virtual void applyPatch(const std::u8string& id, const nlohmann::json& patch) = 0;
  virtual bool isFavorite(std::u8string_view page, uint64_t effectId) const = 0;
  virtual bool setFavorite(std::u8string_view page, uint64_t effectId, bool favorite) = 0;
  virtual nlohmann::json favorites() const = 0;
  virtual bool replaceFavorites(const nlohmann::json& favorites) = 0;
};

// UI-thread cache. Storage, normalization and effect execution belong to luma.
class SettingConfig final : public ISettingConfig {
public:
  SettingConfig();
  ~SettingConfig() override;
  void load() override;
  nlohmann::json defaultConfig() const override {
      return defaults_;
  }
  nlohmann::json config(const std::u8string& id) const override;
  bool setConfig(const std::u8string& id, nlohmann::json& value) override;
  void applyPatch(const std::u8string& id, const nlohmann::json& patch) override;
  bool isFavorite(std::u8string_view page, uint64_t effectId) const override;
  bool setFavorite(std::u8string_view page, uint64_t effectId, bool favorite) override;
  nlohmann::json favorites() const override {
      return favorites_;
  }
  bool replaceFavorites(const nlohmann::json& favorites) override;

private:
  void updateSnapshot(const nlohmann::json& body);
  bool post(std::string_view method, nlohmann::json body = nlohmann::json::object()) const;
  std::u8string uid_;
  nlohmann::json defaults_ = {{"version", 1}};
  nlohmann::json favorites_ = {{"version", 1}, {"pages", nlohmann::json::object()}};
  mutable std::map<std::u8string, nlohmann::json> configs_;
};
