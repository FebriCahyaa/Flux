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

#include "RuntimeHost.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace flux::perf {

namespace {

std::optional<std::string> read_fd_all(int fd) {
    if (fd < 0) return std::nullopt;
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0) {
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > (1u << 20)) break; // nodes and journals are small; bound the read
    }
    ::close(fd);
    if (n < 0) return std::nullopt;
    return out;
}

bool write_fd_all(int fd, const std::string &text) {
    size_t off = 0;
    while (off < text.size()) {
        const ssize_t n = ::write(fd, text.data() + off, text.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

bool is_virtual_block(const std::string &dev) {
    for (const char *p : {"loop", "ram", "zram", "dm-", "md", "sr", "fd", "nbd"})
        if (dev.rfind(p, 0) == 0) return true;
    return dev.find("boot") != std::string::npos || dev.find("rpmb") != std::string::npos;
}

} // namespace

flux::runtime::Io make_node_io(const std::string &root) {
    flux::runtime::Io io;
    io.exists = [root](const std::string &p) { return ::access((root + p).c_str(), F_OK) == 0; };
    io.read = [root](const std::string &p) { return read_fd_all(::open((root + p).c_str(), O_RDONLY | O_CLOEXEC)); };
    io.write = [root](const std::string &p, const std::string &v) {
        // No O_CREAT: a node that does not exist is never created. No O_NOFOLLOW: sysfs uses links.
        const int fd = ::open((root + p).c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
        if (fd < 0) return false;
        const bool ok = write_fd_all(fd, v);
        return ::close(fd) == 0 && ok;
    };
    return io;
}

FileStore make_file_store() {
    FileStore fs;
    fs.read = [](const std::string &p) { return read_fd_all(::open(p.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)); };
    fs.write_atomic = [](const std::string &p, const std::string &text) {
        struct stat st {};
        if (::lstat(p.c_str(), &st) == 0 && !S_ISREG(st.st_mode)) return false; // never replace a symlink/dir
        const std::string tmp = p + ".tmp." + std::to_string(::getpid());
        const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) return false;
        const bool ok = write_fd_all(fd, text) && ::fsync(fd) == 0;
        if (::close(fd) != 0 || !ok || ::rename(tmp.c_str(), p.c_str()) != 0) {
            ::unlink(tmp.c_str());
            return false;
        }
        return true;
    };
    fs.remove = [](const std::string &p) { return ::unlink(p.c_str()) == 0 || errno == ENOENT; };
    return fs;
}

std::vector<int> parse_panel_rates(const std::string &text) {
    std::vector<int> out;
    for (size_t at = text.find("fps="); at != std::string::npos; at = text.find("fps=", at + 4)) {
        char *end = nullptr;
        const double v = std::strtod(text.c_str() + at + 4, &end);
        if (end == text.c_str() + at + 4 || v < 1 || v > 1000) continue;
        const int hz = static_cast<int>(v + 0.5);
        if (std::find(out.begin(), out.end(), hz) == out.end()) out.push_back(hz);
    }
    std::sort(out.rbegin(), out.rend());
    return out;
}

PerfCapabilities probe_capabilities(const std::string &root, std::function<std::vector<int>()> panel_rates) {
    PerfCapabilities caps;
    caps.node_available = [root](const std::string &p) { return ::access((root + p).c_str(), R_OK) == 0; };
    if (DIR *d = ::opendir((root + "/sys/block").c_str())) {
        while (const dirent *e = ::readdir(d)) {
            const std::string dev = e->d_name;
            if (dev.empty() || dev[0] == '.' || is_virtual_block(dev)) continue;
            const std::string base = "/sys/block/" + dev;
            if (auto rm = read_fd_all(::open((root + base + "/removable").c_str(), O_RDONLY | O_CLOEXEC)))
                if (!rm->empty() && (*rm)[0] == '1') continue;
            if (::access((root + base + "/queue").c_str(), F_OK) == 0) caps.block_queues.push_back(base + "/queue");
        }
        ::closedir(d);
    }
    std::sort(caps.block_queues.begin(), caps.block_queues.end());
    if (panel_rates) caps.panel_refresh_hz = panel_rates();
    return caps;
}

// -- RuntimeHost ---------------------------------------------------------------------------------

std::vector<RecoveryResult> RuntimeHost::on_daemon_start() { return rt_.recover(); }

void RuntimeHost::on_game_active(const std::string &package, int pid, int64_t now_ms) {
    if (!enabled_) return;
    rt_.on_game_start(package, pid, now_ms); // idempotent for the same package + pid
}

void RuntimeHost::on_profile_applied() {
    if (enabled_) rt_.after_profile_script();
}

bool RuntimeHost::on_game_end(EndReason why) { return rt_.on_game_end(why); }

void RuntimeHost::tick(int64_t now_ms) { rt_.tick(now_ms); }

int RuntimeHost::refresh_request_hz() const {
    return enabled_ && rt_.state() == RuntimeState::Active ? rt_.refresh_target_hz() : 0;
}

void RuntimeHost::set_enabled(bool enabled) {
    if (enabled_ && !enabled) rt_.on_game_end(EndReason::DaemonStop);
    enabled_ = enabled;
}

} // namespace flux::perf
