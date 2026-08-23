/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#ifndef HAILORT_DEBUG_SHELL_HPP_
#define HAILORT_DEBUG_SHELL_HPP_

#include "hrpc_protocol/serializer.hpp"

namespace hailort
{

class DebugShellRunner final
{
public:
    static Expected<DebugShellExecSerializer::Reply> execute(const DebugShellExecSerializer::Request &request);

    DebugShellRunner() = delete;
};

} /* namespace hailort */

#endif /* HAILORT_DEBUG_SHELL_HPP_ */
