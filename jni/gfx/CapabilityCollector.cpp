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

#include "CapabilityCollector.hpp"

#include <chrono>
#include <exception>

namespace flux::gfx {

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void run_collectors(const std::vector<std::shared_ptr<CapabilityCollector>> &collectors, CapabilityModel &out) {
    if (out.generated_at_ms == 0) out.generated_at_ms = now_ms();

    for (const auto &collector : collectors) {
        if (!collector) continue;

        CapabilitySource source;
        try {
            source = collector->collect(out);
        } catch (const std::exception &e) {
            // A probe that throws is a broken probe, not a broken daemon: record it
            // and carry on so the remaining collectors still contribute.
            source.id = collector->id();
            source.domains = collector->domains();
            source.status = schema::source_status::kError;
            source.detail = std::string("collector threw: ") + e.what();
            source.collected_at_ms = now_ms();
        } catch (...) {
            source.id = collector->id();
            source.domains = collector->domains();
            source.status = schema::source_status::kError;
            source.detail = "collector threw a non-standard exception";
            source.collected_at_ms = now_ms();
        }

        if (source.id.empty()) source.id = collector->id();
        out.add_source(source);
    }
}

} // namespace flux::gfx
