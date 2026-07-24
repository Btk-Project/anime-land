#pragma once

#include <QUrl>

#include "bangumi/config.hpp"
#include "common/app_settings.hpp"

#include <span>
#include <string>
#include <vector>

namespace anime_land {

/** User-facing metadata for one checkbox on Bangumi's app settings page. */
struct BangumiCapabilityInfo {
    BangumiCapability capability = BangumiCapability::None;
    QString resourceName;
    QString accessName;
    QString permissionName;
    QString description;
};

/** A feature declares capabilities without depending on any concrete View. */
struct BangumiFeatureDeclaration {
    std::string id;
    QString name;
    QString description;
    BangumiCapability requiredCapabilities = BangumiCapability::None;
};

/** Composition-root input; feature modules append their declarations here. */
struct BangumiModuleOptions {
    std::vector<BangumiFeatureDeclaration> features;
};

struct BangumiCapabilityRequirement {
    BangumiCapabilityInfo capability;
    std::vector<BangumiFeatureDeclaration> features;
};

/** Complete dynamic guide consumed by CLI now and a Qt dialog later. */
struct BangumiOAuthApplicationGuide {
    QUrl applicationPageUrl;
    QString redirectUri;
    std::vector<BangumiCapabilityRequirement> requiredCapabilities;
    std::vector<BangumiCapabilityInfo> optionalCapabilities;
    bool requiresCrossOrigin = false;
};

auto allBangumiCapabilities() -> std::span<const BangumiCapability>;
auto bangumiCapabilityInfo(BangumiCapability capability) -> BangumiCapabilityInfo;
auto buildBangumiOAuthApplicationGuide(const BangumiSettings &settings, const BangumiModuleOptions &options) -> BangumiOAuthApplicationGuide;

auto missingBangumiCapabilityError(BangumiCapability capability, const BangumiFeatureDeclaration &feature, const BangumiSettings &settings) -> BangumiError;

} // namespace anime_land
