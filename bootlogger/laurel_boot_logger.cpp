/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr char kLogRoot[] = "/metadata/boot-debug";
constexpr auto kCaptureDuration = std::chrono::minutes(5);
constexpr auto kSnapshotInterval = std::chrono::seconds(10);
constexpr size_t kMaxKernelLogBytes = 512 * 1024;

std::string ReadFile(const std::string& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) return {};

    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::string Trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

bool MetadataIsMounted() {
    std::ifstream mounts("/proc/mounts");
    std::string device;
    std::string mount_point;
    std::string filesystem;

    while (mounts >> device >> mount_point >> filesystem) {
        std::string ignored;
        std::getline(mounts, ignored);
        if (mount_point == "/metadata") return true;
    }
    return false;
}

std::string RandomId() {
    std::array<unsigned char, 16> random{};
    if (getrandom(random.data(), random.size(), 0) !=
        static_cast<ssize_t>(random.size())) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::to_string(now) + "-" + std::to_string(getpid());
    }

    std::ostringstream id;
    id << std::hex << std::setfill('0');
    for (const auto byte : random) id << std::setw(2) << static_cast<unsigned>(byte);
    return id.str();
}

std::string BootId() {
    std::string id = Trim(ReadFile("/proc/sys/kernel/random/boot_id"));
    if (id.empty()) id = RandomId();

    for (char& character : id) {
        const bool safe = (character >= 'a' && character <= 'z') ||
                          (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '-';
        if (!safe) character = '_';
    }
    return id;
}

std::string CreateBootDirectory() {
    if (mkdir(kLogRoot, 0700) != 0 && errno != EEXIST) return {};

    const std::string base = std::string(kLogRoot) + "/boot-" + BootId();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const std::string candidate = attempt == 0 ? base : base + "-" + std::to_string(attempt);
        if (mkdir(candidate.c_str(), 0700) == 0) return candidate;
        if (errno != EEXIST) return {};
    }
    return {};
}

void AppendFile(const std::string& destination, const std::string& heading,
                const std::string& source) {
    std::ofstream output(destination, std::ios::out | std::ios::app);
    if (!output) return;

    output << "\n===== " << heading << " =====\n";
    const std::string contents = ReadFile(source);
    output << (contents.empty() ? "<unavailable>\n" : contents);
}

void AppendDirectory(const std::string& destination, const std::string& path) {
    std::ofstream output(destination, std::ios::out | std::ios::app);
    if (!output) return;

    output << "\n----- " << path << " -----\n";
    DIR* directory = opendir(path.c_str());
    if (directory == nullptr) {
        output << "<unavailable: " << strerror(errno) << ">\n";
        return;
    }

    while (dirent* entry = readdir(directory)) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        const std::string item = path + "/" + entry->d_name;
        std::array<char, 512> target{};
        const ssize_t length = readlink(item.c_str(), target.data(), target.size() - 1);
        output << entry->d_name;
        if (length > 0) output << " -> " << std::string(target.data(), length);
        output << '\n';
    }
    closedir(directory);
}

void AppendDmState(const std::string& destination) {
    DIR* sys_block = opendir("/sys/block");
    if (sys_block == nullptr) return;

    while (dirent* entry = readdir(sys_block)) {
        if (strncmp(entry->d_name, "dm-", 3) != 0) continue;
        const std::string base = std::string("/sys/block/") + entry->d_name;
        AppendFile(destination, base + "/dm/name", base + "/dm/name");
        AppendFile(destination, base + "/dm/uuid", base + "/dm/uuid");
        AppendDirectory(destination, base + "/slaves");
    }
    closedir(sys_block);
}

void WriteSnapshot(const std::string& path, unsigned sequence) {
    std::ofstream output(path, std::ios::out | std::ios::app);
    if (!output) return;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    output << "\n######## snapshot " << sequence << " monotonic_ms=" << elapsed.count()
           << " ########\n";
    output.close();

    AppendFile(path, "/proc/mounts", "/proc/mounts");
    AppendFile(path, "SELinux enforcing", "/sys/fs/selinux/enforce");
    AppendDirectory(path, "/dev/block/mapper");
    AppendDirectory(path, "/dev/block/by-name");
    AppendDmState(path);

    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        syncfs(fd);
        close(fd);
    }
}

pid_t StartLogcat(const std::string& destination) {
    const pid_t child = fork();
    if (child != 0) return child;

    execl("/system/bin/logcat", "logcat", "-b", "all", "-v", "threadtime", "-f",
          destination.c_str(), "-r", "512", "-n", "1", "*:V", nullptr);
    _exit(127);
}

void CaptureKernelLog(const std::string& destination) {
    const int input = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    const int output = open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (input < 0 || output < 0) {
        if (input >= 0) close(input);
        if (output >= 0) close(output);
        return;
    }

    std::array<char, 4096> buffer{};
    size_t written = 0;
    const auto deadline = std::chrono::steady_clock::now() + kCaptureDuration;
    while (std::chrono::steady_clock::now() < deadline && written < kMaxKernelLogBytes) {
        const ssize_t count = read(input, buffer.data(), buffer.size());
        if (count > 0) {
            const ssize_t result = write(output, buffer.data(), static_cast<size_t>(count));
            if (result <= 0) break;
            written += static_cast<size_t>(result);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    fsync(output);
    close(output);
    close(input);
}

}  // namespace

int main() {
    // Never write into the empty rootfs mount point: only real metadata survives reboot.
    for (unsigned attempt = 0; attempt < 30 && !MetadataIsMounted(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!MetadataIsMounted()) return 1;

    const std::string directory = CreateBootDirectory();
    if (directory.empty()) return 1;

    const std::string info = directory + "/boot-info.txt";
    {
        std::ofstream output(info, std::ios::out | std::ios::trunc);
        output << "directory=" << directory << '\n';
        output << "capture_duration_seconds="
               << std::chrono::duration_cast<std::chrono::seconds>(kCaptureDuration).count()
               << '\n';
    }
    AppendFile(info, "/proc/cmdline", "/proc/cmdline");
    AppendFile(info, "/proc/version", "/proc/version");
    AppendFile(info, "/proc/filesystems", "/proc/filesystems");

    const pid_t logcat = StartLogcat(directory + "/logcat.txt");
    std::thread kernel_thread(CaptureKernelLog, directory + "/kernel.log");

    const std::string state = directory + "/state.log";
    const auto deadline = std::chrono::steady_clock::now() + kCaptureDuration;
    unsigned sequence = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        WriteSnapshot(state, sequence++);
        std::this_thread::sleep_for(kSnapshotInterval);
    }

    if (logcat > 0) {
        kill(logcat, SIGTERM);
        waitpid(logcat, nullptr, 0);
    }
    kernel_thread.join();
    sync();
    return 0;
}
