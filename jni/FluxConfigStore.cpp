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

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "FluxConfigStore.hpp"

bool FluxConfigStore::load_config(const std::string &config_path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_path_ = config_path;
    }

    FILE *fp = fopen(config_path.c_str(), "rb");
    if (!fp) {
        LOGW_TAG("FluxConfigStore", "Config file not found, creating default: {}", config_path);
        return create_default_config();
    }

    char readBuffer[65536];
    rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));

    rapidjson::Document doc;
    doc.ParseStream(is);
    fclose(fp);

    if (doc.HasParseError()) {
        LOGE_TAG(
            "FluxConfigStore",
            "Parse error: {} (Offset: {})",
            rapidjson::GetParseError_En(doc.GetParseError()),
            doc.GetErrorOffset()
        );
        return create_default_config();
    }

    if (!doc.IsObject()) {
        LOGE_TAG("FluxConfigStore", "Root is not an object");
        return false;
    }

    return parse_config(doc);
}

bool FluxConfigStore::save_config(const std::string &config_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType &allocator = doc.GetAllocator();

    // Serialize preferences
    rapidjson::Value prefs_obj(rapidjson::kObjectType);
    prefs_obj.AddMember("enforce_lite_mode", config_.preferences.enforce_lite_mode, allocator);
    prefs_obj.AddMember("use_device_mitigation", config_.preferences.use_device_mitigation, allocator);
    prefs_obj.AddMember("disable_tweaks", config_.preferences.disable_tweaks, allocator);
    prefs_obj.AddMember("flux_sched", config_.preferences.flux_sched, allocator);
    prefs_obj.AddMember("flux_vm", config_.preferences.flux_vm, allocator);
    prefs_obj.AddMember("flux_io", config_.preferences.flux_io, allocator);
    prefs_obj.AddMember("game_priority", config_.preferences.game_priority, allocator);
    prefs_obj.AddMember("net_tweaks", config_.preferences.net_tweaks, allocator);
    prefs_obj.AddMember("touch_tweaks", config_.preferences.touch_tweaks, allocator);
    prefs_obj.AddMember("game_refresh_rate", config_.preferences.game_refresh_rate, allocator);
    prefs_obj.AddMember("drop_caches", config_.preferences.drop_caches, allocator);
    prefs_obj.AddMember("surface_boost", config_.preferences.surface_boost, allocator);
    prefs_obj.AddMember("chipset_boost", config_.preferences.chipset_boost, allocator);
    prefs_obj.AddMember("sustained_mode", config_.preferences.sustained_mode, allocator);
    prefs_obj.AddMember("render_boost", config_.preferences.render_boost, allocator);
    prefs_obj.AddMember("render_realtime", config_.preferences.render_realtime, allocator);
    prefs_obj.AddMember("gpu_power_lock", config_.preferences.gpu_power_lock, allocator);
    prefs_obj.AddMember("adreno_reflex", config_.preferences.adreno_reflex, allocator);
    prefs_obj.AddMember("graphics_tweaks", config_.preferences.graphics_tweaks, allocator);
    prefs_obj.AddMember("adaptive_refresh", config_.preferences.adaptive_refresh, allocator);
    prefs_obj.AddMember("zram_tune", config_.preferences.zram_tune, allocator);
    prefs_obj.AddMember("log_level", config_.preferences.log_level, allocator);
    doc.AddMember("preferences", prefs_obj, allocator);

    // Serialize CPU governor
    rapidjson::Value cpu_gov_obj(rapidjson::kObjectType);
    cpu_gov_obj.AddMember("balance", rapidjson::Value(config_.cpu_governor.balance.c_str(), allocator).Move(), allocator);
    cpu_gov_obj.AddMember("powersave", rapidjson::Value(config_.cpu_governor.powersave.c_str(), allocator).Move(), allocator);
    doc.AddMember("cpu_governor", cpu_gov_obj, allocator);

    // Serialize GPU governor
    rapidjson::Value gpu_gov_obj(rapidjson::kObjectType);
    gpu_gov_obj.AddMember("balance", rapidjson::Value(config_.gpu_governor.balance.c_str(), allocator).Move(), allocator);
    gpu_gov_obj.AddMember("powersave", rapidjson::Value(config_.gpu_governor.powersave.c_str(), allocator).Move(), allocator);
    doc.AddMember("gpu_governor", gpu_gov_obj, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    writer.SetIndent(' ', 2);
    doc.Accept(writer);

    FILE *fp = fopen(config_path.c_str(), "wb");
    if (!fp) {
        LOGE_TAG("FluxConfigStore", "Failed to open config file for writing: {}", config_path);
        return false;
    }

    fwrite(buffer.GetString(), 1, buffer.GetSize(), fp);
    fclose(fp);

    LOGI_TAG("FluxConfigStore", "Configuration saved to {}", config_path);
    return true;
}

FluxConfigStore::ConfigData FluxConfigStore::get_config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

FluxConfigStore::Preferences FluxConfigStore::get_preferences() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.preferences;
}

FluxConfigStore::GPUGovernor FluxConfigStore::get_gpu_governor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.gpu_governor;
}

FluxConfigStore::CPUGovernor FluxConfigStore::get_cpu_governor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.cpu_governor;
}

void FluxConfigStore::set_preferences(const Preferences &prefs) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.preferences = prefs;
}

void FluxConfigStore::set_cpu_governor(const CPUGovernor &governor) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.cpu_governor = governor;
}

std::string FluxConfigStore::get_config_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_path_;
}

bool FluxConfigStore::reload() {
    return load_config(config_path_);
}

std::string FluxConfigStore::read_default_cpu_governor() const {
    // Fallback governor
    std::string default_governor = "schedutil";

    std::ifstream file(DEFAULT_CPU_GOV);
    if (!file.is_open()) {
        LOGW_TAG("FluxConfigStore", "Default CPU governor file not found, using fallback: {}", default_governor);
    }

    if (std::getline(file, default_governor)) {
        LOGD_TAG("FluxConfigStore", "Read default CPU governor from file: {}", default_governor);
    } else {
        LOGW_TAG("FluxConfigStore", "Default CPU governor file is empty, using fallback: {}", default_governor);
    }

    file.close();
    return default_governor;
}

bool FluxConfigStore::create_default_config() {
    std::string default_governor = read_default_cpu_governor();

    // clang-format off
    ConfigData default_config = ConfigData{
        .preferences = {
            .enforce_lite_mode = false,
            .use_device_mitigation = false,
            .disable_tweaks = false,
            .flux_sched = true,
            .flux_vm = true,
            .flux_io = true,
            .game_priority = true,
            .net_tweaks = true,
            .touch_tweaks = true,
            .game_refresh_rate = false,
            .drop_caches = true,
            .surface_boost = true,
            .chipset_boost = true,
            .sustained_mode = true,
            .render_boost = true,
            .render_realtime = false,
            .gpu_power_lock = false,
            .adreno_reflex = false,
            .graphics_tweaks = false,
            .adaptive_refresh = false,
            .zram_tune = false,
            .log_level = 4
        },
        .cpu_governor = {
            .balance = default_governor,
            .powersave = default_governor
        },
        .gpu_governor = {}
    };
    // clang-format on

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = default_config;
    }

    return save_config(config_path_);
}

bool FluxConfigStore::parse_config(const rapidjson::Document &doc) {
    ConfigData new_config;

    // Parse preferences
    if (doc.HasMember("preferences") && doc["preferences"].IsObject()) {
        const rapidjson::Value &prefs = doc["preferences"];

        if (prefs.HasMember("enforce_lite_mode") && prefs["enforce_lite_mode"].IsBool()) {
            new_config.preferences.enforce_lite_mode = prefs["enforce_lite_mode"].GetBool();
        }

        if (prefs.HasMember("use_device_mitigation") && prefs["use_device_mitigation"].IsBool()) {
            new_config.preferences.use_device_mitigation = prefs["use_device_mitigation"].GetBool();
        }

        if (prefs.HasMember("disable_tweaks") && prefs["disable_tweaks"].IsBool()) {
            new_config.preferences.disable_tweaks = prefs["disable_tweaks"].GetBool();
        }

        if (prefs.HasMember("flux_sched") && prefs["flux_sched"].IsBool()) {
            new_config.preferences.flux_sched = prefs["flux_sched"].GetBool();
        }

        // Flux Boost switches; missing keys (older configs) keep their defaults.
        for (const auto &[key, field] : {std::pair{"flux_vm", &Preferences::flux_vm},
                                         std::pair{"flux_io", &Preferences::flux_io},
                                         std::pair{"game_priority", &Preferences::game_priority},
                                         std::pair{"net_tweaks", &Preferences::net_tweaks},
                                         std::pair{"touch_tweaks", &Preferences::touch_tweaks},
                                         std::pair{"game_refresh_rate", &Preferences::game_refresh_rate},
                                         std::pair{"drop_caches", &Preferences::drop_caches},
                                         std::pair{"surface_boost", &Preferences::surface_boost},
                                         std::pair{"chipset_boost", &Preferences::chipset_boost},
                                         std::pair{"sustained_mode", &Preferences::sustained_mode},
                                         std::pair{"render_boost", &Preferences::render_boost},
                                         std::pair{"render_realtime", &Preferences::render_realtime},
                                         std::pair{"gpu_power_lock", &Preferences::gpu_power_lock},
                                         std::pair{"adreno_reflex", &Preferences::adreno_reflex},
                                         std::pair{"graphics_tweaks", &Preferences::graphics_tweaks},
                                         std::pair{"adaptive_refresh", &Preferences::adaptive_refresh},
                                         std::pair{"zram_tune", &Preferences::zram_tune}}) {
            if (prefs.HasMember(key) && prefs[key].IsBool()) {
                new_config.preferences.*field = prefs[key].GetBool();
            }
        }

        if (prefs.HasMember("log_level") && prefs["log_level"].IsInt()) {
            new_config.preferences.log_level = prefs["log_level"].GetInt();
        }
    }

    // Parse CPU governor
    if (doc.HasMember("cpu_governor") && doc["cpu_governor"].IsObject()) {
        const rapidjson::Value &gov = doc["cpu_governor"];

        if (gov.HasMember("balance") && gov["balance"].IsString()) {
            new_config.cpu_governor.balance = gov["balance"].GetString();
        }

        if (gov.HasMember("powersave") && gov["powersave"].IsString()) {
            new_config.cpu_governor.powersave = gov["powersave"].GetString();
        }
    }

    // Parse GPU governor (absent in older configs: keep the kernel's governor)
    if (doc.HasMember("gpu_governor") && doc["gpu_governor"].IsObject()) {
        const rapidjson::Value &gov = doc["gpu_governor"];
        if (gov.HasMember("balance") && gov["balance"].IsString()) {
            new_config.gpu_governor.balance = gov["balance"].GetString();
        }
        if (gov.HasMember("powersave") && gov["powersave"].IsString()) {
            new_config.gpu_governor.powersave = gov["powersave"].GetString();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = new_config;
    }

    LOGI_TAG("FluxConfigStore", "Configuration loaded from {}", config_path_);
    return true;
}
