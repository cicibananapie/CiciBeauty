// SPDX-FileCopyrightText: BANANAPIE LIMITED
// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSES/GPL-2.0-or-later.txt for the full license text.

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "nlohmann/json_fwd.hpp"

class LoginLogic;
class EffectCatalog;
class ObsInfo;
class AssetsMgr;
class UpdateClient;
class ISettingConfig;
class SubscriptionController;

class FilterModel {
public:
	virtual ~FilterModel() = default;
	virtual const std::u8string& filterUuid() const = 0;
	virtual ObsInfo *obsInfo() const = 0;
	virtual std::u8string sourceUuid() const = 0;
	virtual bool effectParamValue(const char8_t *jsonPath, int &value) const = 0;
	virtual bool defaultEffectParamValue(const char8_t *jsonPath, int &value) const = 0;
	virtual bool canActivateEffect(uint64_t effectId,
	                               nlohmann::json& disablePatch) const = 0;
	virtual void retryEffectPackageDownload(uint64_t effectId) = 0;
        virtual void applySettingPatch(const nlohmann::json& patch) = 0;
        virtual bool replaceSettingConfig(nlohmann::json& config) = 0;
};

class IFilterModelChangeObserver {
public:
	virtual ~IFilterModelChangeObserver() = default;
	virtual void onFilterModelChanged(FilterModel *oldModel, FilterModel *newModel) = 0;
};

class IEffectConflictResolver {
public:
	virtual ~IEffectConflictResolver() = default;
	virtual bool resolveEffectConflicts(const nlohmann::json& disablePatch) = 0;
};

class GlobalModel {
public:
	virtual ~GlobalModel() = default;
	virtual AssetsMgr *assets() const = 0;
	virtual LoginLogic *loginLogic() const = 0;
	virtual SubscriptionController *subscriptionController() const = 0;
	virtual ISettingConfig *settingConfig() const = 0;
	virtual const EffectCatalog *effectCatalog() const = 0;
	virtual FilterModel *filterModel() = 0;
	virtual bool hasCiciFilterOnSource(const std::u8string &sourceUuid) = 0;
	virtual bool applyCiciFilterToSource(const std::u8string &sourceUuid,
	                                     const std::u8string &trigger) = 0;
	virtual bool removeCiciFilterFromSource(const std::u8string &sourceUuid,
	                                        const std::u8string &trigger) = 0;
	virtual void registerFilterModelChangeObserver(IFilterModelChangeObserver *observer) = 0;
	virtual void unregisterFilterModelChangeObserver(IFilterModelChangeObserver *observer) = 0;
};
