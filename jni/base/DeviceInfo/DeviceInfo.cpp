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

#include <DeviceInfo.hpp>
#include <FluxLog.hpp>
#include <fstream>
#include <sys/system_properties.h>
#include <sys/utsname.h>

// --- Public API ---

const std::string &DeviceInfo::get_kernel_uname() {
    static const std::string cached = fetch_kernel_uname();
    return cached;
}

const std::string &DeviceInfo::get_soc_model() {
    static const std::string cached = fetch_soc_model();
    return cached;
}

const std::string &DeviceInfo::get_device_model() {
    static const std::string cached = fetch_device_model();
    return cached;
}

// --- Private ---

std::string DeviceInfo::fetch_kernel_uname() {
    struct utsname buffer;
    if (uname(&buffer) != 0) {
        LOGE_TAG("DeviceInfo", "uname failed: {}", strerror(errno));
        return "Unknown";
    }
    return std::string(buffer.release);
}

namespace {

std::string get_system_property(const char *name) {
    char prop_value[PROP_VALUE_MAX];
    int len = __system_property_get(name, prop_value);
    if (len <= 0) return {};
    std::string result(prop_value, len);
    size_t end = result.find_last_not_of(" \t\r\n");
    return end == std::string::npos ? std::string{} : result.substr(0, end + 1);
}

} // namespace

std::string DeviceInfo::fetch_soc_model() {
    // ro.board.platform / ro.soc.model / ro.hardware carry the chip codename
    // (e.g. "sdm660") on every ROM, stock or custom, and are what device
    // mitigation rules below are written against. /proc/device-tree/model is
    // the board string instead — it varies far more (sometimes blank or a
    // device codename on custom kernels) and previously left device rules
    // for older Snapdragon chips silently unmatched on many real devices.
    for (const char *prop : {"ro.board.platform", "ro.soc.model", "ro.hardware"}) {
        std::string value = get_system_property(prop);
        if (!value.empty()) return value;
    }

    std::ifstream file("/proc/device-tree/model");
    if (!file.is_open()) {
        return "Unknown";
    }

    std::string model;
    std::getline(file, model);

    size_t null_pos = model.find('\0');
    if (null_pos != std::string::npos) {
        model = model.substr(0, null_pos);
    }

    model.erase(model.find_last_not_of(" \t\n\r\f\v") + 1);
    return model.empty() ? "Unknown" : model;
}

std::string DeviceInfo::fetch_device_model() {
    std::string result = get_system_property("ro.product.model");
    return result.empty() ? "Unknown" : result;
}
