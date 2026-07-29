#include "process.hpp"

#include "common/log.hpp"

#include <QLocale>

#include <clocale>

#if defined(_WIN32)
    #include <Windows.h>
#endif

namespace anime_land {

ProcessGuard::ProcessGuard() {
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

ProcessGuard::~ProcessGuard() { shutdownLogging(); }

} // namespace anime_land
