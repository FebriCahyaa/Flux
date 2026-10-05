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

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "FluxCLI.hpp"

#include <Flux.hpp>
#include <GameRegistry.hpp>

#include <CapabilityCollector.hpp>
#include <PlatformProbe.hpp>
#include <VulkanProbe.hpp>

#include "InstallationEpoch.hpp"
#include "TelemetryStore.hpp"

#include <chrono>

std::string get_module_version() {
    std::ifstream prop_file(MODULE_PROP);
    std::string line;
    std::string version = "unknown";

    if (!prop_file.is_open()) {
        std::cerr << "\033[33mERROR:\033[0m Could not open " << MODULE_PROP << std::endl;
        return version;
    }

    while (std::getline(prop_file, line)) {
        if (line.find("version=") == 0) {
            // Remove "version=" prefix
            version = line.substr(8);

            // Remove quotes if present
            if (!version.empty() && version.front() == '"' && version.back() == '"') {
                version = version.substr(1, version.length() - 2);
            }

            break;
        }
    }

    prop_file.close();
    return version;
}

int version_handler(const std::vector<std::string> &args) {
    (void)args;

    std::string module_version = get_module_version();
    std::cout << "Flux Tweaks " << module_version << std::endl;
    std::cout << "Built on " << __TIME__ << " " << __DATE__ << std::endl;
    return EXIT_SUCCESS;
}

int setup_gamelist_handler(const std::vector<std::string> &args) {
    bool success = GameRegistry::populate_from_base(FLUX_GAMELIST, args[0]);
    if (!success) {
        std::cerr << "\033[31mERROR:\033[0m Failed to setup gamelist from " << args[0] << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int check_gamelist_handler(const std::vector<std::string> &args) {
    (void)args;

    if (access(FLUX_GAMELIST, F_OK) != 0) {
        std::cout << "\033[33mERROR:\033[0m " << FLUX_GAMELIST << " does not exist" << std::endl;
        return EXIT_FAILURE;
    }

    GameRegistry registry;
    if (!registry.load_from_json(FLUX_GAMELIST)) {
        std::cerr << "\033[31mERROR:\033[0m Failed to parse " << FLUX_GAMELIST << std::endl;
        return EXIT_FAILURE;
    }

    // stderr output is intentional for module installation
    std::cerr << FLUX_GAMELIST << " is valid" << std::endl;
    std::cerr << "Registered games: " << registry.size() << std::endl;
    return EXIT_SUCCESS;
}

int daemon_handler(const std::vector<std::string> &args) {
    (void)args;

    // This will be implemented in Main.cpp
    extern int run_daemon();
    return run_daemon();
}

/**
 * Collect the canonical capability model and print it, optionally writing it to
 * a file as well.
 *
 * Capability collection runs here, in a short-lived CLI process, rather than in
 * the daemon: a driver that misbehaves while being probed can then take nothing
 * with it but this invocation. The daemon reads the resulting file; it never
 * hosts the probe itself. Later phases consume the model, and no part of this
 * build interprets it.
 */
int capabilities_handler(const std::vector<std::string> &args) {
    using namespace flux::gfx;

    const SystemQuery query = default_system_query();
    CapabilityModel model;

    run_collectors(
        {
            std::make_shared<VulkanCollector>(),
            std::make_shared<GpuSysfsCollector>(query),
            std::make_shared<DisplayCollector>(query),
            std::make_shared<HwcCollector>(query),
            std::make_shared<RenderEngineCollector>(query),
            std::make_shared<RuntimeCollector>(query),
        },
        model);

    std::cout << model.to_json() << std::endl;

    if (!args.empty()) {
        if (!model.write_file(args[0])) {
            std::cerr << "\033[31mERROR:\033[0m Could not write " << args[0] << std::endl;
            return EXIT_FAILURE;
        }
    }

    // A device with no Vulkan is a supported device, so an unavailable collector
    // is not a failure of this command.
    return EXIT_SUCCESS;
}


// Device validation hook for persistent telemetry (Step 8.11). Read-only: opens the store
// read-only; `retention` is a dry run (fluxd owns the real cleanup).
int telemetry_handler(const std::vector<std::string> &args) {
    namespace o = flux::observatory;
    const o::TelemetryClock clock{
        [] {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
        },
        [] {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch()).count());
        }};
    o::PersistentEventStore store(o::kTelemetryRoot, clock, o::make_posix_io());
    if (!store.open(true)) {
        std::cerr << "telemetry unavailable: " << store.health().last_error << std::endl;
        return EXIT_FAILURE;
    }
    const std::string &sub = args[0];
    if (sub == "status") {
        o::QueryStats st;
        auto all = store.query({}, &st);
        auto io = o::make_posix_io();
        auto epoch = io->read_all(std::string(o::kTelemetryRoot) + "/installation.json");
        auto parsed = epoch ? o::epoch_from_json(*epoch) : std::nullopt;
        std::cout << "format: ok" << std::endl
                  << "segments: " << st.segments_total << std::endl
                  << "events: " << all.size() << std::endl
                  << "corrupted_lines: " << st.corrupted_lines << std::endl
                  << "oldest_ms: " << (all.empty() ? 0 : all.front().timestamp_ms) << std::endl
                  << "newest_ms: " << (all.empty() ? 0 : all.back().timestamp_ms) << std::endl
                  << "installation_id: " << (parsed ? parsed->installation_id : "unavailable") << std::endl;
        return EXIT_SUCCESS;
    }
    if (sub == "retention") {
        auto r = store.maintain(true);
        std::cout << "dry_run: true" << std::endl
                  << "ran: " << (r.ran ? "true" : "false") << std::endl
                  << "skipped: " << r.skipped_reason << std::endl
                  << "cutoff_ms: " << r.cutoff_ms << std::endl
                  << "segments_to_delete: " << r.segments_deleted << std::endl
                  << "segments_to_trim: " << r.segments_trimmed << std::endl
                  << "records_to_remove: " << r.records_removed << std::endl;
        return EXIT_SUCCESS;
    }
    if (sub == "query") {
        o::TelemetryQuery q;
        for (size_t i = 1; i < args.size(); ++i) {
            const auto eq = args[i].find('=');
            if (eq == std::string::npos) {
                std::cerr << "expected key=value, got " << args[i] << std::endl;
                return EXIT_FAILURE;
            }
            const std::string k = args[i].substr(0, eq), v = args[i].substr(eq + 1);
            try {
                if (k == "session") q.session_id = v;
                else if (k == "package") q.package = v;
                else if (k == "source") q.source = v;
                else if (k == "type") q.type = v;
                else if (k == "tx") q.transaction_id = v;
                else if (k == "severity") q.min_severity = o::parse_severity(v);
                else if (k == "from") q.from_ms = std::stoll(v);
                else if (k == "to") q.to_ms = std::stoll(v);
                else if (k == "limit") q.limit = std::stoul(v);
                else {
                    std::cerr << "unknown filter " << k << std::endl;
                    return EXIT_FAILURE;
                }
            } catch (...) {
                std::cerr << "invalid value for " << k << std::endl;
                return EXIT_FAILURE;
            }
        }
        for (const auto &e : store.query(q)) std::cout << o::to_json(e) << std::endl;
        return EXIT_SUCCESS;
    }
    std::cerr << "unknown telemetry subcommand: " << sub << std::endl;
    return EXIT_FAILURE;
}

// clang-format off
std::vector<CliCommand> commands = {
    {
        "daemon",
        "Start Flux Tweaks daemon",
        "daemon",
        0,
        0,
        daemon_handler
    },
    {
        "setup_gamelist",
        "Setup initial gamelist from base file",
        "setup_gamelist <base_file_path>",
        1,
        1,
        setup_gamelist_handler
    },
    {
        "check_gamelist",
        "Validate gamelist file",
        "check_gamelist",
        0,
        0,
        check_gamelist_handler
    },
    {
        "capabilities",
        "Probe graphics capabilities and print the canonical model",
        "capabilities [output_file]",
        0,
        1,
        capabilities_handler
    },
    {
        "telemetry",
        "Inspect persistent telemetry (read-only; retention is a dry run)",
        "telemetry status | retention | query [session=|package=|source=|type=|tx=|severity=|from=|to=|limit=]...",
        1,
        10,
        telemetry_handler
    },
    {
        "version",
        "Show version information",
        "version",
        0,
        0,
        version_handler
    },
};
// clang-format on

void cli_usage(const char *program_name) {
    std::cout << "Flux Tweaks CLI" << std::endl << std::endl;
    std::cout << "Usage: " << program_name << " <COMMAND>" << std::endl << std::endl;
    std::cout << "Commands:" << std::endl;

    for (const auto &cmd : commands) {
        printf("  %-20s %s\n", cmd.name.c_str(), cmd.description.c_str());
    }

    std::cout << std::endl << "Options:" << std::endl;
    std::cout << "  -h, --help          Show this help message" << std::endl;
    std::cout << "  -V, --version       Show version information" << std::endl;
    std::cout << std::endl << "Run '" << program_name << " <COMMAND> --help' for more information on a command." << std::endl;
}

void cli_usage_command(const CliCommand &cmd) {
    std::cout << "Usage: fluxd " << cmd.usage << std::endl << std::endl;
    std::cout << cmd.description << std::endl;
}

int flux_cli(int argc, char *argv[]) {
    const char *program_name = argv[0];

    if (argc == 1) {
        cli_usage(program_name);
        return EXIT_FAILURE;
    }

    std::string command = argv[1];
    std::vector<std::string> args;

    for (int i = 2; i < argc; i++) {
        args.push_back(argv[i]);
    }

    if (command == "-h" || command == "--help") {
        cli_usage(program_name);
        return EXIT_SUCCESS;
    }

    if (command == "-V" || command == "--version") {
        return version_handler({});
    }

    if (args.size() == 1 && (args[0] == "-h" || args[0] == "--help")) {
        for (const auto &cmd : commands) {
            if (cmd.name != command) continue;
            cli_usage_command(cmd);
            return EXIT_SUCCESS;
        }

        std::cerr << "\033[31mERROR:\033[0m Unknown command: " << command << std::endl;
        return EXIT_FAILURE;
    }

    for (const auto &cmd : commands) {
        if (cmd.name == command) {
            size_t min_args = static_cast<size_t>(cmd.min_args);
            size_t max_args = static_cast<size_t>(cmd.max_args);

            if (args.size() < min_args || args.size() > max_args) {
                std::cerr << "\033[31mERROR:\033[0m Invalid number of arguments for '" << command << "'" << std::endl;
                cli_usage_command(cmd);
                return EXIT_FAILURE;
            }

            return cmd.handler(args);
        }
    }

    std::cerr << "\033[31mERROR:\033[0m Unknown command: " << command << std::endl;
    std::cerr << "See '" << program_name << " --help' for available commands." << std::endl;
    return EXIT_FAILURE;
}
