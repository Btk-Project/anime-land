/**
 * @file qt_version.hpp
 * @brief anime-land 的 Qt 源码兼容边界。
 */
#pragma once

#include <QtGlobal>

#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
#error "anime-land requires Qt 6.2 or later"
#endif

#if QT_VERSION >= QT_VERSION_CHECK(7, 0, 0)
#error "anime-land currently supports Qt 6.x"
#endif
