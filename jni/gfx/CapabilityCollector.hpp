/*
 * Copyright (C) 2024-2026 FebriCahyaa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CapabilityModel.hpp"

namespace flux::gfx {

/**
 * @brief A source of capability facts that fluxd is best positioned to observe.
 *
 * SynthesisCore owns the canonical model; a collector only contributes to it.
 * The contract is deliberately narrow:
 *
 *  - collect() writes observable facts and nothing else. No ranking, no scoring,
 *    no "recommended backend": interpretation belongs above this layer.
 *  - collect() must not throw and must not abort. A device that cannot answer is
 *    a normal outcome, reported as an `unavailable` source with the facts left
 *    absent. Capability collection never blocks daemon startup.
 *  - A fact that was not observed is omitted, never defaulted. Absent means
 *    "unsupported or not observed"; it never means false or zero.
 */
class CapabilityCollector {
public:
    virtual ~CapabilityCollector() = default;

    /// Stable collector id recorded as provenance, e.g. "fluxd.vulkan".
    virtual std::string id() const = 0;

    /// Domains this collector may contribute to.
    virtual std::vector<std::string> domains() const = 0;

    /**
     * @brief Observe the device and write facts into @p out.
     * @return the run's outcome, to be recorded in the model's sources array.
     */
    virtual CapabilitySource collect(CapabilityModel &out) = 0;
};

/**
 * @brief Run @p collectors in order, folding each one's facts into @p out.
 *
 * Every collector runs even if an earlier one failed, and any exception escaping a
 * collector is caught and recorded as an `error` source rather than propagated —
 * a misbehaving probe degrades the model, it does not take the caller down.
 * Earlier collectors win on conflicting keys, matching CapabilityModel::merge.
 */
void run_collectors(const std::vector<std::shared_ptr<CapabilityCollector>> &collectors, CapabilityModel &out);

/// Wall-clock milliseconds since the epoch; used to stamp collection times.
int64_t now_ms();

} // namespace flux::gfx
