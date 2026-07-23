#include "presentation/cli/bangumi_cli_view.hpp"

#include <ilias/io.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <string_view>
#include <iostream>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

namespace anime_land::cli {
namespace {

/** Disable terminal echo only while a sensitive line is being entered. */
class ConsoleEchoGuard {
public:
    explicit ConsoleEchoGuard(bool disabled) {
        if (!disabled) {
            return;
        }
#if defined(_WIN32)
        mInput = ::GetStdHandle(STD_INPUT_HANDLE);
        if (mInput != INVALID_HANDLE_VALUE && ::GetConsoleMode(mInput, &mMode) &&
            ::SetConsoleMode(mInput, mMode & ~ENABLE_ECHO_INPUT)) {
            mActive = true;
        }
#else
        if (::isatty(STDIN_FILENO) == 1 &&
            ::tcgetattr(STDIN_FILENO, &mAttributes) == 0) {
            auto attributes = mAttributes;
            attributes.c_lflag &= static_cast<tcflag_t>(~ECHO);
            mActive = ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &attributes) == 0;
        }
#endif
    }

    ~ConsoleEchoGuard() {
        if (!mActive) {
            return;
        }
#if defined(_WIN32)
        static_cast<void>(::SetConsoleMode(mInput, mMode));
#else
        static_cast<void>(::tcsetattr(STDIN_FILENO, TCSAFLUSH, &mAttributes));
#endif
    }

    ConsoleEchoGuard(const ConsoleEchoGuard &) = delete;
    auto operator=(const ConsoleEchoGuard &) -> ConsoleEchoGuard & = delete;

private:
    bool mActive = false;
#if defined(_WIN32)
    HANDLE mInput = INVALID_HANDLE_VALUE;
    DWORD mMode = 0;
#else
    termios mAttributes {};
#endif
};

auto trimInput(std::string value) -> std::string {
    if (value.ends_with('\r')) {
        value.pop_back();
    }
    const auto notSpace = [](unsigned char character) {
        return !std::isspace(character);
    };
    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

template <typename Reader>
auto requestRequiredLine(Reader &reader, ilias::Stdout &output,
                         std::string_view prompt, bool sensitive,
                         std::size_t maximumLength)
    -> ilias::Task<BangumiResult<std::string>> {
    // 在BUG修好先 先用用堵塞IO把 
    while (true) {
        std::puts(std::string {prompt}.c_str());
        // auto written = co_await output.writeAll(ilias::makeBuffer(prompt));
        // auto flushed = co_await output.flush();
        // if (!written || !flushed) {
        //   co_return ilias::Err(
        //       bangumiError(BangumiErrorCode::InvalidConfiguration,
        //                    QStringLiteral("无法向命令行输出参数提示")));
        // }

        auto line = [&]() -> ilias::Task<ilias::IoResult<std::string>> {
            // ConsoleEchoGuard echoGuard(sensitive);
            // co_return co_await reader.getline();
            std::string line;
            std::cin >> line;
            co_return line;
        }();
        auto value = co_await std::move(line);
        if (sensitive) {
            // getline consumes the Enter key while terminal echo is disabled.
            // auto newline = co_await output.writeAll(ilias::makeBuffer("\n"));
            // static_cast<void>(co_await output.flush());
            // if (!newline) {
            //   co_return ilias::Err(
            //       bangumiError(BangumiErrorCode::InvalidConfiguration,
            //                    QStringLiteral("无法恢复命令行输出")));
            // }
            std::putchar('\n');
        }
        if (!value) {
            co_return ilias::Err(bangumiError(
                BangumiErrorCode::InvalidConfiguration,
                QStringLiteral("无法从标准输入读取必填参数（输入已结束）")));
        }

        auto trimmed = trimInput(std::move(*value));
        if (trimmed.size() > maximumLength) {
            auto warning = co_await output.writeAll(
                ilias::makeBuffer("输入内容过长，请检查后重新输入。\n"));
            static_cast<void>(co_await output.flush());
            if (!warning) {
                co_return ilias::Err(
                    bangumiError(BangumiErrorCode::InvalidConfiguration,
                                 QStringLiteral("无法向命令行输出参数提示")));
            }
            continue;
        }
        if (!trimmed.empty()) {
            co_return trimmed;
        }
        auto warning = co_await output.writeAll(
            ilias::makeBuffer("该项不能为空，请重新输入。\n"));
        static_cast<void>(co_await output.flush());
        if (!warning) {
            co_return ilias::Err(
                bangumiError(BangumiErrorCode::InvalidConfiguration,
                             QStringLiteral("无法向命令行输出参数提示")));
        }
    }
}

} // namespace

void BangumiCliView::showState(BangumiLoginState state) {
    std::cerr << "[bangumi] state: " << bangumiLoginStateName(state) << '\n';
}

void BangumiCliView::showUser(const BangumiUser &user) {
    std::cout << "Bangumi login verified\n"
              << "  id: " << user.id << '\n'
              << "  username: " << user.username.toStdString() << '\n'
              << "  nickname: " << user.nickname.toStdString() << '\n';
}

void BangumiCliView::showCollections(
    const BangumiUserCollectionsResponse &response) {
    std::cout.write(response.rawBody.constData(), response.rawBody.size());
    if (response.rawBody.isEmpty() || !response.rawBody.endsWith('\n')) {
        std::cout.put('\n');
    }
    std::cout.flush();
}

void BangumiCliView::showError(const BangumiError &error) {
    std::cerr << "[bangumi] " << bangumiErrorCodeName(error.code) << ": "
              << error.message.toStdString() << '\n';
}

void BangumiCliView::showMessage(QStringView message) {
    std::cout << message.toString().toStdString() << '\n';
}

auto BangumiCliView::requestOAuthApplication(
    const BangumiOAuthApplicationGuide &guide)
    -> ilias::Task<BangumiResult<BangumiOAuthApplication>> {
    std::cout << "\n尚未配置 Bangumi OAuth 应用。请使用你自己的 Bangumi "
                 "账号创建应用：\n"
              << "  1. 登录 Bangumi 后打开："
              << guide.applicationPageUrl.toString().toStdString() << '\n'
              << "  2. 创建“应用”，并把回调地址完整填写为："
              << guide.redirectUri.toStdString() << '\n'
              << "  3. 为当前启用的功能勾选以下最低权限：\n";
    for (const auto &requirement : guide.requiredCapabilities) {
        std::cout << "     [必需] "
                  << requirement.capability.permissionName.toStdString()
                  << " —— 用于 ";
        for (std::size_t index = 0; index < requirement.features.size(); ++index) {
            if (index != 0) {
                std::cout << "、";
            }
            std::cout << requirement.features[index].name.toStdString();
        }
        std::cout << '\n';
    }
    std::cout << "     当前不需要的可选权限：";
    for (std::size_t index = 0; index < guide.optionalCapabilities.size();
         ++index) {
        if (index != 0) {
            std::cout << "、";
        }
        std::cout << guide.optionalCapabilities[index].resourceName.toStdString()
                  << ' '
                  << guide.optionalCapabilities[index].accessName.toStdString();
    }
    std::cout
        << "\n     请遵循最小权限原则；以后启用对应功能时再增加。\n"
        << "     跨域请求："
        << (guide.requiresCrossOrigin ? "需要勾选" : "桌面客户端不需要勾选")
        << "\n  4. 创建后复制页面顶部的 App ID 和 App Secret。App ID 不是用户名，"
           "App Secret 也不是账户密码。\n"
        << "输入的参数会先完成 App 存在性、OAuth 与 /v0/me 验证，成功后才保存；"
           "App Secret 不会回显。\n\n";
    std::cout.flush();
    auto reader = ilias::BufReader {ilias::Stdin {}};
    auto output = ilias::Stdout {};

    auto clientId = co_await requestRequiredLine(
        reader, output, "Bangumi App ID (client_id): ", false, 512);
    if (!clientId) {
        co_return ilias::Err(std::move(clientId.error()));
    }
    auto clientSecret = co_await requestRequiredLine(
        reader, output, "Bangumi App Secret (client_secret，输入不回显): ", true,
        4096);
    if (!clientSecret) {
        co_return ilias::Err(std::move(clientSecret.error()));
    }
    co_return BangumiOAuthApplication {
        QString::fromUtf8(clientId->data(),
                          static_cast<qsizetype>(clientId->size())),
        std::move(*clientSecret)};
}

} // namespace anime_land::cli
