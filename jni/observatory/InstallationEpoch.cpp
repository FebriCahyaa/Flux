#include "InstallationEpoch.hpp"

#include <cctype>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace flux::observatory {

namespace {

bool hex32(const std::string &s) {
    if (s.size() != 32) return false;
    for (unsigned char c : s)
        if (!std::isxdigit(c)) return false;
    return true;
}

} // namespace

std::string epoch_to_json(const InstallationEpoch &e) {
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    auto str = [&](const char *k, const std::string &v) {
        w.Key(k);
        w.String(v.c_str(), static_cast<rapidjson::SizeType>(v.size()));
    };
    w.StartObject();
    w.Key("format");
    w.Int(e.format);
    str("installation_id", e.installation_id);
    w.Key("installed_at_ms");
    w.Int64(e.installed_at_ms);
    str("first_version", e.first_version);
    str("first_android_version", e.first_android_version);
    str("first_kernel_version", e.first_kernel_version);
    str("first_device_identity", e.first_device_identity);
    str("architecture", e.architecture);
    w.EndObject();
    return std::string(sb.GetString()) + "\n";
}

std::optional<InstallationEpoch> epoch_from_json(const std::string &text) {
    rapidjson::Document d;
    d.Parse(text.c_str(), text.size());
    if (d.HasParseError() || !d.IsObject()) return std::nullopt;
    auto s = [&](const char *k) -> std::optional<std::string> {
        auto it = d.FindMember(k);
        if (it == d.MemberEnd() || !it->value.IsString()) return std::nullopt;
        return std::string(it->value.GetString(), it->value.GetStringLength());
    };
    auto f = d.FindMember("format");
    auto at = d.FindMember("installed_at_ms");
    if (f == d.MemberEnd() || !f->value.IsInt() || f->value.GetInt() != 1) return std::nullopt;
    if (at == d.MemberEnd() || !at->value.IsInt64()) return std::nullopt;
    InstallationEpoch e;
    auto id = s("installation_id");
    if (!id || !hex32(*id)) return std::nullopt;
    e.installation_id = *id;
    e.installed_at_ms = at->value.GetInt64();
    for (auto [k, out] : {std::pair{"first_version", &e.first_version}, {"first_android_version", &e.first_android_version},
                          {"first_kernel_version", &e.first_kernel_version},
                          {"first_device_identity", &e.first_device_identity}, {"architecture", &e.architecture}}) {
        auto v = s(k);
        if (!v) return std::nullopt;
        *out = *v;
    }
    return e;
}

EpochResult load_or_create_epoch(FileIo &io, const std::string &root, const EpochFacts &facts, int64_t wall_ms,
                                 const std::function<std::string()> &random_hex32) {
    EpochResult r;
    const std::string path = root + "/installation.json";
    try {
        if (io.exists(path)) {
            auto text = io.read_all(path);
            r.epoch = text ? epoch_from_json(*text) : std::nullopt;
            if (!r.epoch) r.error = path + " unreadable or corrupt; preserved, not overwritten";
            return r;
        }
        InstallationEpoch e;
        e.installation_id = random_hex32 ? random_hex32() : "";
        if (!hex32(e.installation_id)) {
            r.error = "no random source for installation_id";
            return r;
        }
        e.installed_at_ms = wall_ms >= kMinValidWallMs ? wall_ms : 0; // 0 = clock was not set
        e.first_version = facts.version;
        e.first_android_version = facts.android_version;
        e.first_kernel_version = facts.kernel_version;
        e.first_device_identity = facts.device_identity;
        e.architecture = facts.architecture;
        if (!io.mkdirs(root) || !io.write_atomic(path, epoch_to_json(e))) {
            r.error = "cannot write " + path;
            return r;
        }
        r.epoch = e;
        r.created = true;
    } catch (const std::exception &ex) {
        r.error = ex.what();
    }
    return r;
}

} // namespace flux::observatory
