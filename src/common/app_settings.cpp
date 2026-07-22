#include "./app_settings.hpp"
#include "./log.hpp"

#include <nekoproto/serialization/toml_serializer.hpp>

#include <sodium.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace anime_land {
namespace {

constexpr std::string_view kEncryptedSecretPrefix = "encrypted:v1:";
using SecretKey = std::array<unsigned char, crypto_secretbox_KEYBYTES>;

auto secretKeyPath(const std::filesystem::path &settingsPath)
    -> std::filesystem::path {
  auto path = settingsPath;
  path += ".key";
  return path;
}

void wipe(std::string &value) noexcept {
  if (!value.empty()) {
    sodium_memzero(value.data(), value.size());
    value.clear();
  }
}

auto loadSecretKey(const std::filesystem::path &settingsPath, bool create)
    -> std::optional<SecretKey> {
  if (sodium_init() < 0) {
    AL_LOG_WARN("Failed to initialize libsodium for app settings");
    return std::nullopt;
  }

  const auto path = secretKeyPath(settingsPath);
  SecretKey key{};
  if (std::filesystem::exists(path)) {
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char *>(key.data()),
               static_cast<std::streamsize>(key.size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) {
      sodium_memzero(key.data(), key.size());
      AL_LOG_WARN("Invalid app settings key file: {}", path.string());
      return std::nullopt;
    }
    return key;
  }

  if (!create) {
    AL_LOG_WARN("App settings key file not found: {}", path.string());
    return std::nullopt;
  }

  const auto parent = path.parent_path();
  std::error_code error;
  if (!parent.empty() && !std::filesystem::exists(parent) &&
      !std::filesystem::create_directories(parent, error)) {
    AL_LOG_WARN("Failed to create app settings key directory: {}",
                error.message());
    return std::nullopt;
  }

  randombytes_buf(key.data(), key.size());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(key.data()),
               static_cast<std::streamsize>(key.size()));
  output.close();
  if (!output) {
    sodium_memzero(key.data(), key.size());
    std::filesystem::remove(path, error);
    AL_LOG_WARN("Failed to write app settings key file: {}", path.string());
    return std::nullopt;
  }

  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, error);
  if (error) {
    sodium_memzero(key.data(), key.size());
    std::filesystem::remove(path, error);
    AL_LOG_WARN("Failed to restrict app settings key permissions: {}",
                path.string());
    return std::nullopt;
  }
  return key;
}

auto encryptSecret(std::string_view plaintext, const SecretKey &key)
    -> std::optional<std::string> {

  std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
  randombytes_buf(nonce.data(), nonce.size());
  std::vector<unsigned char> packed(nonce.size() + plaintext.size() +
                                    crypto_secretbox_MACBYTES);
  std::copy(nonce.begin(), nonce.end(), packed.begin());
  if (crypto_secretbox_easy(
          packed.data() + nonce.size(),
          reinterpret_cast<const unsigned char *>(plaintext.data()),
          static_cast<unsigned long long>(plaintext.size()), nonce.data(),
          key.data()) != 0) {
    return std::nullopt;
  }

  std::vector<char> encoded(sodium_base64_ENCODED_LEN(
      packed.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING));
  sodium_bin2base64(encoded.data(), encoded.size(), packed.data(),
                    packed.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
  sodium_memzero(packed.data(), packed.size());
  return std::string(kEncryptedSecretPrefix) + encoded.data();
}

auto decryptSecret(std::string_view ciphertext, const SecretKey &key)
    -> std::optional<std::string> {
  if (!ciphertext.starts_with(kEncryptedSecretPrefix)) {
    return std::string(ciphertext);
  }

  ciphertext.remove_prefix(kEncryptedSecretPrefix.size());
  std::vector<unsigned char> packed(ciphertext.size());
  std::size_t packedSize = 0;
  if (sodium_base642bin(packed.data(), packed.size(), ciphertext.data(),
                        ciphertext.size(), nullptr, &packedSize, nullptr,
                        sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
    return std::nullopt;
  }
  packed.resize(packedSize);
  if (packed.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) {
    return std::nullopt;
  }

  const auto *nonce = packed.data();
  const auto *encrypted = packed.data() + crypto_secretbox_NONCEBYTES;
  const auto encryptedSize = packed.size() - crypto_secretbox_NONCEBYTES;
  std::string plaintext(encryptedSize - crypto_secretbox_MACBYTES, '\0');
  const int status = crypto_secretbox_open_easy(
      reinterpret_cast<unsigned char *>(plaintext.data()), encrypted,
      static_cast<unsigned long long>(encryptedSize), nonce, key.data());
  sodium_memzero(packed.data(), packed.size());
  if (status != 0) {
    wipe(plaintext);
    return std::nullopt;
  }
  return plaintext;
}

template <typename T>
auto encryptSecretObject(T &object, const SecretKey &key) {
  if constexpr (NEKO_NAMESPACE::detail::has_names_meta<T>) {
    Reflect<T>::forEach(object, [&](auto &field, std::string_view name,
                                    auto &tags) {
      if constexpr (tag_query::get<tag_properties::encrypt>(decltype(tags){})) {
        if (!field.empty()) {
          auto encrypted = encryptSecret(field, key);
          if (!encrypted) {
            wipe(field);
            AL_LOG_WARN("Failed to encrypt config secret for: {}", name);
            return;
          }
          wipe(field);
          field = std::move(*encrypted);
        }
      } else {
        return encryptSecretObject(field, key);
      }
    });
  }
}

template <typename T>
void decryptSecretObject(T &object, const SecretKey &key) {
  if constexpr (NEKO_NAMESPACE::detail::has_names_meta<T>) {
    Reflect<T>::forEach(object, [&](auto &field, std::string_view name,
                                    auto &tags) {
      if constexpr (tag_query::get<tag_properties::encrypt>(decltype(tags){})) {
        if (!field.empty()) {
          auto encrypted = decryptSecret(field, key);
          if (!encrypted) {
            wipe(field);
            AL_LOG_WARN("Failed to decrypt config secret for: {}", name);
            return;
          }
          wipe(field);
          field = std::move(*encrypted);
        }
      } else {
        return decryptSecretObject(field, key);
      }
    });
  }
}
} // namespace

Mutex GlobalAppSettingGuard::mMutex;
AppSettings GlobalAppSettingGuard::mAppSettings;

auto GlobalAppSettingGuard::load(const std::filesystem::path &path) -> bool {
  AL_LOG_DEBUG("[app.settings] load started path={}", path.string());
  if (!std::filesystem::exists(path)) {
    AL_LOG_WARN("App settings file not found: {}", path.string());
    return false;
  }
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    AL_LOG_WARN("Failed to open app settings file: {}", path.string());
    return false;
  }
  TomlplusplusSerializer::InputSerializer serializer(ifs);
  AppSettings loaded;
  auto ret = serializer(loaded);
  ifs.close();
  if (!ret) {
    AL_LOG_WARN("Failed to load app settings: {}", serializer.error()->msg);
    return false;
  }
  auto key = loadSecretKey(path, false);
  if (!key) {
    AL_LOG_WARN("Failed to load secret key for app settings: {}",
                path.string());
    return false;
  }
  decryptSecretObject(loaded, *key);
  sodium_memzero(key->data(), key->size());
  {
    auto settings = get();
    *settings = std::move(loaded);
  }
  // Transparently migrate an older plaintext secret on the first successful
  // load. The in-memory representation remains plaintext for the OAuth client.
  if (!save(path)) {
    AL_LOG_WARN("Failed to migrate plaintext Bangumi client secret: {}",
                path.string());
    return false;
  }
  AL_LOG_INFO("[app.settings] load completed");
  return true;
}

auto GlobalAppSettingGuard::loadOrCreate(const std::filesystem::path &path)
    -> std::optional<AppSettingsFileState> {
  if (std::filesystem::exists(path)) {
    if (!load(path)) {
      return std::nullopt;
    }
    return AppSettingsFileState::Loaded;
  }

  {
    auto settings = get();
    *settings = AppSettings{};
  }
  if (!save(path)) {
    return std::nullopt;
  }
  return AppSettingsFileState::Created;
}

auto GlobalAppSettingGuard::save(const std::filesystem::path &path) -> bool {
  AL_LOG_DEBUG("[app.settings] save started path={}", path.string());
  // check if file exists and is writable or can be created
  if (!path.has_filename()) {
    AL_LOG_WARN("Invalid app settings file path: {}", path.string());
    return false;
  }
  if (!std::filesystem::exists(path)) {
    const auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent) &&
        !std::filesystem::create_directories(parent)) {
      AL_LOG_WARN("Failed to create directory for app settings file: {}",
                  path.string());
      return false;
    }
  }

  AppSettings persisted;
  {
    auto settings = get();
    persisted = *settings;
  }
  auto key = loadSecretKey(path, true);
  if (!key) {
    AL_LOG_WARN("Failed to load secret key for app settings file: {}",
                path.string());
  } else {
    encryptSecretObject(persisted, *key);
    sodium_memzero(key->data(), key->size());
  }

  std::vector<char> buffer;
  {
    TomlplusplusSerializer::OutputSerializer serializer(buffer);
    auto ret = serializer(persisted);
    if (!ret || !serializer.end()) {
      AL_LOG_WARN("Failed to save app settings: {}", serializer.error()->msg);
      return false;
    }
  }

  std::ofstream ofs(path, std::ios::binary);
  if (!ofs.is_open()) {
    AL_LOG_WARN("Failed to open app settings file for writing: {}",
                path.string());
    return false;
  }
  ofs.write(buffer.data(), buffer.size());
  ofs.close();
  if (!ofs) {
    AL_LOG_WARN("Failed to write app settings file: {}", path.string());
    return false;
  }
  AL_LOG_INFO("[app.settings] save completed");
  return true;
}

} // namespace anime_land
