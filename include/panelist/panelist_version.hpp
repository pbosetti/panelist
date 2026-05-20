#ifndef PANELIST_PANELIST_VERSION_HPP
#define PANELIST_PANELIST_VERSION_HPP

/**
 * @file panelist_version.hpp
 * @brief Fallback Panelist version string for direct source includes.
 */

#include <string_view>

namespace Panelist {

/**
 * @brief Panelist version string.
 *
 * This fallback header is used when the source include directory is consumed
 * without CMake. CMake builds generate a replacement header from
 * `panelist_version.hpp.in` and put it earlier on the include path.
 */
inline constexpr std::string_view Version = "v0.0.0";

} // namespace Panelist

#endif // PANELIST_PANELIST_VERSION_HPP
