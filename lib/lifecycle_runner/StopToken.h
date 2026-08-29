#pragma once

#include <stop_token>

namespace RSCGroup {

/**
 * @brief Portable stop-request/token pair (C++20 style).
 *
 * Thin aliases over std::stop_source / std::stop_token so that worker loop
 * code can be written against the RSCGroup namespace and does not need to
 * pull in the full <stop_token> header directly.
 *
 * Usage:
 * @code
 *   StopSource src;
 *   StopToken  tok = src.get_token();
 *
 *   // worker loop:
 *   while (!tok.stop_requested()) { ... }
 *
 *   // from another thread:
 *   src.request_stop();
 * @endcode
 */
using StopToken  = std::stop_token;
using StopSource = std::stop_source;

} // namespace RSCGroup
