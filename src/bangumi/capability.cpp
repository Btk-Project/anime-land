#include "bangumi/capability.hpp"

#include <array>
#include <utility>

namespace anime_land {
namespace {

constexpr std::array kAllCapabilities{
    BangumiCapability::CollectionRead, BangumiCapability::CollectionWrite,
    BangumiCapability::IndexRead,      BangumiCapability::IndexWrite,
    BangumiCapability::TopicRead,      BangumiCapability::TopicWrite,
    BangumiCapability::WikiRead,       BangumiCapability::WikiWrite,
};

} // namespace

auto allBangumiCapabilities() -> std::span<const BangumiCapability> {
  return kAllCapabilities;
}

auto bangumiCapabilityInfo(BangumiCapability capability)
    -> BangumiCapabilityInfo {
  switch (capability) {
  case BangumiCapability::None:
    return {};
  case BangumiCapability::CollectionRead:
    return {capability, QStringLiteral("收藏"), QStringLiteral("READ"),
            QStringLiteral("收藏 READ（获取用户收藏）"),
            QStringLiteral("读取用户收藏及收藏进度")};
  case BangumiCapability::CollectionWrite:
    return {capability, QStringLiteral("收藏"), QStringLiteral("WRITE"),
            QStringLiteral("收藏 WRITE（修改用户收藏）"),
            QStringLiteral("修改收藏状态、评分与观看进度")};
  case BangumiCapability::IndexRead:
    return {capability, QStringLiteral("目录"), QStringLiteral("READ"),
            QStringLiteral("目录 READ（读取目录）"),
            QStringLiteral("读取用户目录")};
  case BangumiCapability::IndexWrite:
    return {capability, QStringLiteral("目录"), QStringLiteral("WRITE"),
            QStringLiteral("目录 WRITE（修改目录）"),
            QStringLiteral("创建或修改目录内容")};
  case BangumiCapability::TopicRead:
    return {capability, QStringLiteral("帖子"), QStringLiteral("READ"),
            QStringLiteral("帖子 READ（读取帖子）"),
            QStringLiteral("读取帖子与回复")};
  case BangumiCapability::TopicWrite:
    return {capability, QStringLiteral("帖子"), QStringLiteral("WRITE"),
            QStringLiteral("帖子 WRITE（发帖/回帖）"),
            QStringLiteral("代表用户发帖或回复")};
  case BangumiCapability::WikiRead:
    return {capability, QStringLiteral("维基"), QStringLiteral("READ"),
            QStringLiteral("维基 READ（获取维基数据）"),
            QStringLiteral("读取维基数据")};
  case BangumiCapability::WikiWrite:
    return {capability, QStringLiteral("维基"), QStringLiteral("WRITE"),
            QStringLiteral("维基 WRITE（进行维基编辑）"),
            QStringLiteral("代表用户编辑维基")};
  }
  return {};
}

auto buildBangumiOAuthApplicationGuide(const BangumiSettings &settings,
                                       const BangumiModuleOptions &options)
    -> BangumiOAuthApplicationGuide {
  BangumiOAuthApplicationGuide guide;
  guide.applicationPageUrl = settings.oauth_application_page;
  guide.redirectUri = settings.redirect_uri.toString();

  for (const auto capability : kAllCapabilities) {
    BangumiCapabilityRequirement requirement{
        .capability = bangumiCapabilityInfo(capability),
        .features = {},
    };
    for (const auto &feature : options.features) {
      if (hasBangumiCapability(feature.requiredCapabilities, capability)) {
        requirement.features.push_back(feature);
      }
    }
    if (requirement.features.empty()) {
      guide.optionalCapabilities.push_back(std::move(requirement.capability));
    } else {
      guide.requiredCapabilities.push_back(std::move(requirement));
    }
  }
  return guide;
}

auto missingBangumiCapabilityError(BangumiCapability capability,
                                   const BangumiFeatureDeclaration &feature,
                                   const BangumiSettings &settings)
    -> BangumiError {
  const auto info = bangumiCapabilityInfo(capability);
  const QUrl &settingsUrl = settings.oauth_application_page;
  BangumiError error{
      .code = BangumiErrorCode::MissingCapability,
      .message =
          QStringLiteral("“%1”需要开启 %2；请在 %3 修改应用权限后重新登录")
              .arg(feature.name, info.permissionName, settingsUrl.toString()),
      .remediation = std::nullopt,
  };
  error.remediation = BangumiCapabilityRemediation{
      .capability = capability,
      .featureId = feature.id,
      .featureName = feature.name,
      .permissionName = info.permissionName,
      .applicationSettingsUrl = settingsUrl,
  };
  return error;
}

} // namespace anime_land
