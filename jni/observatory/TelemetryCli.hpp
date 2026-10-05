// `fluxd telemetry …` (Steps 8.11 / 8.12): read-only commands over the persistent store.
// Opens the store read-only; `retention` is a dry run; analysis never writes.
#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace flux::observatory {

/// status | retention | query [k=v…] | session <id> | analyze <id> | history <package> [from=ms] [to=ms]
/// history without `to` ends at the newest stored event (not the current time), for determinism.
int run_telemetry_command(const std::vector<std::string> &args, const std::string &root, std::ostream &out,
                          std::ostream &err);

} // namespace flux::observatory
