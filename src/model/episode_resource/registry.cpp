#include "pch.hpp"

#include "model/episode_resource/registry.hpp"

#include <algorithm>
#include <utility>

namespace anime_land {
namespace {

auto validProviderKey(QStringView key) -> bool {
    if (key.isEmpty() || key.size() > 160 || key.front() == QLatin1Char('.') ||
        key.back() == QLatin1Char('.')) {
        return false;
    }
    for (const QChar character : key) {
        if (!(character.isLower() || character.isDigit() ||
              character == QLatin1Char('.') || character == QLatin1Char('-'))) {
            return false;
        }
    }
    return true;
}

} // namespace

auto EpisodeProviderRegistry::registerProvider(
    std::shared_ptr<EpisodeProvider> provider) -> EpisodeProviderResult<void> {
    return registerProviders({std::move(provider)});
}

auto EpisodeProviderRegistry::registerProviders(
    const std::vector<std::shared_ptr<EpisodeProvider>> &providers)
    -> EpisodeProviderResult<void> {
    if (providers.empty()) {
        return {};
    }
    const std::lock_guard lock(mMutex);
    std::vector<QString> incoming;
    incoming.reserve(providers.size());
    for (const auto &provider : providers) {
        if (!provider || !validProviderKey(provider->key()) ||
            provider->name().trimmed().isEmpty()) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::InvalidProvider,
                QStringLiteral("Provider 身份或名称无效")));
        }
        if (std::ranges::find(incoming, provider->key()) != incoming.end() ||
            std::ranges::any_of(mProviders, [&](const auto &existing) {
                return existing->key() == provider->key();
            })) {
            return ilias::Err(episodeProviderError(
                EpisodeProviderErrorCode::DuplicateProvider,
                QStringLiteral("Provider key 重复：%1").arg(provider->key())));
        }
        incoming.push_back(provider->key());
    }
    mProviders.insert(mProviders.end(), providers.begin(), providers.end());
    return {};
}

auto EpisodeProviderRegistry::find(QStringView key) const
    -> std::shared_ptr<EpisodeProvider> {
    const std::lock_guard lock(mMutex);
    const auto found = std::ranges::find_if(mProviders, [&](const auto &provider) {
        return provider->key() == key;
    });
    return found == mProviders.end() ? nullptr : *found;
}

auto EpisodeProviderRegistry::list() const
    -> std::vector<std::shared_ptr<EpisodeProvider>> {
    const std::lock_guard lock(mMutex);
    return mProviders;
}

void EpisodeProviderRegistry::removePlugin(QStringView pluginId) {
    const std::lock_guard lock(mMutex);
    const QString prefix = pluginId.toString() + QLatin1Char('.');
    std::erase_if(mProviders, [&](const auto &provider) {
        if (!provider->key().startsWith(prefix)) {
            return false;
        }
        provider->cancel();
        return true;
    });
}

void EpisodeProviderRegistry::clear() {
    const std::lock_guard lock(mMutex);
    for (const auto &provider : mProviders) {
        provider->cancel();
    }
    mProviders.clear();
}

} // namespace anime_land
