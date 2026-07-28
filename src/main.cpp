#include "application.hpp"

#include "common/log.hpp"

#include <QLocale>

#include <clocale>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace {

struct LoggingShutdownGuard {
    ~LoggingShutdownGuard() { anime_land::shutdownLogging(); }
};

void configureProcessLocale() {
    // A C/POSIX LC_CTYPE prevents several Linux input-method modules from
    // committing non-ASCII preedit text correctly.
    std::setlocale(LC_CTYPE, "");
    QLocale::setDefault(QLocale::system());

#if defined(_WIN32)
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif
}

} // namespace

auto main(int argc, char **argv) -> int {
    LoggingShutdownGuard loggingShutdownGuard;
    configureProcessLocale();
    return anime_land::runApplication(argc, argv);
}
