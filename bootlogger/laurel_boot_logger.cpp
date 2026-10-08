// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2026 The LineageOS Project
 *
 */

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/random.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <new>
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
constexpr auto kSnapshotInterval = std::chrono::seconds(2);
// Never delete existing captures. Budget: kernel 768 KiB, logcat 768 KiB,
// state/status/info <= 256 KiB; retain at least 1 MiB on the 10 MiB metadata FS.
constexpr unsigned long long kMinFreeBytes = 3 * 1024 * 1024;
constexpr unsigned long long kReserveBytes = 1024 * 1024;
constexpr size_t kChunkBytes = 256 * 1024;
constexpr size_t kQueueBytes = 1024 * 1024;

uint64_t MonotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Shared between the supervisor and kernel collector process. Disk/sysfs
// workers never own the reader's queue lock while issuing filesystem I/O.
struct CaptureStatus {
    std::atomic<uint64_t> read_ms{0}, write_ms{0}, read_bytes{0}, written_bytes{0};
    std::atomic<uint64_t> dropped_records{0}, sequence_gaps{0}, overruns{0};
    std::atomic<int> reader_errno{0}, writer_errno{0};
    std::atomic<unsigned> reader_phase{0}, writer_phase{0}, snapshot_phase{0};
};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);

std::string ReadFile(const std::string& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) return {};

    std::ostringstream output;
    std::array<char, 16384> contents{};
    input.read(contents.data(), contents.size());
    output.write(contents.data(), input.gcount());
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
        if (mount_point == "/metadata" && filesystem == "ext4") return true;
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
    struct statvfs space {};
    if (statvfs("/metadata", &space) != 0 ||
        static_cast<unsigned long long>(space.f_bavail) * space.f_frsize < kMinFreeBytes) {
        fprintf(stderr, "laurel_boot_logger: insufficient free metadata space\n");
        return {};
    }
    if (mkdir(kLogRoot, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "laurel_boot_logger: mkdir %s: %s\n", kLogRoot, strerror(errno));
        return {};
    }

    const std::string base = std::string(kLogRoot) + "/boot-" + BootId();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const std::string candidate = attempt == 0 ? base : base + "-" + std::to_string(attempt);
        if (mkdir(candidate.c_str(), 0700) == 0) return candidate;
        if (errno != EEXIST) {
            fprintf(stderr, "laurel_boot_logger: mkdir %s: %s\n",
                    candidate.c_str(), strerror(errno));
            return {};
        }
    }
    return {};
}

void AppendFile(const std::string& destination, const std::string& heading,
                const std::string& source) {
    std::ofstream output(destination, std::ios::out | std::ios::app);
    if (!output) return;

    output.seekp(0, std::ios::end);
    const auto position = output.tellp();
    if (position < 0 || position >= 48 * 1024) return;
    const std::string contents = ReadFile(source);
    const std::string entry = "\n===== " + heading + " =====\n" +
            (contents.empty() ? "<unavailable>\n" : contents);
    const size_t remaining = 48 * 1024 - static_cast<size_t>(position);
    output.write(entry.data(), std::min(entry.size(), remaining));
}

void AppendDirectory(const std::string& destination, const std::string& path) {
    std::ofstream output(destination, std::ios::out | std::ios::app);
    if (!output) return;

    output.seekp(0, std::ios::end);
    if (output.tellp() < 0 || output.tellp() >= 48 * 1024) return;
    output << "\n----- " << path << " -----\n";
    DIR* directory = opendir(path.c_str());
    if (directory == nullptr) {
        output << "<unavailable: " << strerror(errno) << ">\n";
        return;
    }

    unsigned entries = 0;
    while (dirent* entry = readdir(directory)) {
        if (++entries > 128 || output.tellp() >= 48 * 1024) break;
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

    unsigned devices = 0;
    while (dirent* entry = readdir(sys_block)) {
        if (devices >= 8) break;
        if (strncmp(entry->d_name, "dm-", 3) != 0) continue;
        ++devices;
        const std::string base = std::string("/sys/block/") + entry->d_name;
        AppendFile(destination, base + "/dm/name", base + "/dm/name");
        AppendFile(destination, base + "/dm/uuid", base + "/dm/uuid");
        AppendDirectory(destination, base + "/slaves");
    }
    closedir(sys_block);
}

void WriteSnapshot(const std::string& path, unsigned sequence) {
    // Keep the latest state, not hundreds of repeated directory listings.
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) return;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    output << "\n######## snapshot " << sequence << " monotonic_ms=" << elapsed.count()
           << " ########\n";
    output.close();

    AppendFile(path, "modules", "/proc/modules");
    AppendFile(path, "ath10k srcversion", "/sys/module/ath10k_snoc/srcversion");
    AppendFile(path, "qmi_only", "/sys/module/ath10k_snoc/parameters/qmi_only");
    AppendFile(path, "radio build properties", "/vendor/build.prop");
    AppendFile(path, "regulator ownership", "/sys/kernel/debug/regulator/regulator_summary");
    AppendFile(path, "/proc/mounts", "/proc/mounts");
    AppendFile(path, "SELinux enforcing", "/sys/fs/selinux/enforce");
    AppendDirectory(path, "/dev/block/mapper");
    AppendDirectory(path, "/dev/block/by-name");
    AppendDmState(path);

    const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        // Bound even a surprisingly large device-mapper listing.
        struct stat st {};
        if (fstat(fd, &st) == 0 && st.st_size > 64 * 1024) ftruncate(fd, 64 * 1024);
        fdatasync(fd);
        close(fd);
    }
}

pid_t StartLogcat(const std::string& destination) {
    const pid_t child = fork();
    if (child != 0) return child;

    execl("/system/bin/logcat", "logcat", "-b", "all", "-v", "threadtime", "-f",
          destination.c_str(), "-r", "256", "-n", "2", "*:V", nullptr);
    _exit(127);
}

void CaptureKernelLog(const std::string& directory, CaptureStatus* status) {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> queue;
    size_t queued_bytes = 0;
    bool done = false;
    std::atomic<bool> writer_failed{false};
    const auto deadline = std::chrono::steady_clock::now() + kCaptureDuration;

    std::thread writer([&] {
        int fd = -1;
        size_t chunk_bytes = 0;
        bool first = true;
        uint64_t last_flush = MonotonicMs();
        auto fail = [&](int error) {
            status->writer_errno = error;
            status->writer_phase = 5;
            writer_failed = true;
            fprintf(stderr, "laurel_boot_logger: kernel writer failed: %s\n", strerror(error));
        };
        auto open_chunk = [&] {
            status->writer_phase = 1;
            const std::string path = directory + (first ? "/kernel.log" : "/kernel-tail.log");
            fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
            if (fd < 0) fail(errno);
            chunk_bytes = 0;
        };
        open_chunk();
        while (!writer_failed) {
            std::string record;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(100), [&] { return done || !queue.empty(); });
                if (!queue.empty()) {
                    record = std::move(queue.front());
                    queue.pop_front();
                    queued_bytes -= record.size();
                } else if (done) {
                    break;
                }
            }
            if (!record.empty()) {
                if (chunk_bytes + record.size() > kChunkBytes) {
                    status->writer_phase = 3;
                    if (fdatasync(fd) < 0) { fail(errno); break; }
                    close(fd);
                    fd = -1;
                    if (!first && rename((directory + "/kernel-tail.log").c_str(),
                                         (directory + "/kernel-tail.log.1").c_str()) < 0) {
                        fail(errno);
                        break;
                    }
                    first = false;
                    open_chunk();
                    if (writer_failed) break;
                }
                size_t offset = 0;
                status->writer_phase = 2;
                while (offset < record.size()) {
                    const ssize_t count = write(fd, record.data() + offset, record.size() - offset);
                    if (count < 0 && errno == EINTR) continue;
                    if (count <= 0) { fail(count < 0 ? errno : EIO); break; }
                    offset += count;
                    chunk_bytes += count;
                    status->written_bytes.fetch_add(count);
                    status->write_ms = MonotonicMs();
                }
            }
            if (MonotonicMs() - last_flush >= 1000 && !writer_failed) {
                status->writer_phase = 3;
                if (fdatasync(fd) < 0) { fail(errno); break; }
                last_flush = MonotonicMs();
            }
            if (!writer_failed) status->writer_phase = 0;
        }
        if (fd >= 0) {
            status->writer_phase = 3;
            if (fdatasync(fd) < 0) fail(errno);
            close(fd);
        }
        if (!writer_failed) status->writer_phase = 4;
    });

    status->reader_phase = 1;
    const int input = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (input < 0) status->reader_errno = errno;
    std::array<char, 4097> buffer{}; // ACK PRINTK_MESSAGE_MAX is 2048.
    uint64_t previous_seq = 0;
    bool have_seq = false;
    while (input >= 0 && !writer_failed && std::chrono::steady_clock::now() < deadline) {
        status->reader_phase = 2;
        const ssize_t count = read(input, buffer.data(), buffer.size() - 1);
        if (count > 0) {
            status->read_ms = MonotonicMs();
            status->read_bytes.fetch_add(count);
            buffer[count] = 0;
            unsigned long long seq = 0;
            if (sscanf(buffer.data(), "%*u,%llu,", &seq) == 1) {
                if (have_seq && seq > previous_seq + 1) status->sequence_gaps.fetch_add(seq - previous_seq - 1);
                previous_seq = seq;
                have_seq = true;
            }
            std::string record(buffer.data(), count);
            {
                std::lock_guard<std::mutex> lock(mutex);
                while (!queue.empty() && queued_bytes + record.size() > kQueueBytes) {
                    queued_bytes -= queue.front().size();
                    queue.pop_front();
                    status->dropped_records.fetch_add(1);
                }
                queued_bytes += record.size();
                queue.push_back(std::move(record));
            }
            wake.notify_one();
        } else if (count < 0 && errno == EPIPE) {
            status->overruns.fetch_add(1);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else if (count < 0 && errno == EAGAIN) {
            status->reader_phase = 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } else {
            status->reader_errno = count < 0 ? errno : EIO;
            break;
        }
    }
    if (input >= 0) close(input);
    status->reader_phase = status->reader_errno ? 5 : 4;
    if (status->reader_errno) fprintf(stderr, "laurel_boot_logger: kernel reader failed: %s\n",
                                    strerror(status->reader_errno.load()));
    {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
    }
    wake.notify_one();
    writer.join(); // Only this child can block here; supervisor has bounded shutdown.
}

void WriteStatus(const std::string& path, const CaptureStatus* s, int kernel_exit, int logcat_exit) {
    std::ofstream out(path, std::ios::trunc);
    out << "monotonic_ms=" << MonotonicMs() << "\n"
        << "reader_phase=" << s->reader_phase.load() << " writer_phase=" << s->writer_phase.load() << "\n"
        << "phase_key=0:idle,1:open,2:io,3:flush,4:finished,5:error\n"
        << "snapshot_phase=" << s->snapshot_phase.load() << "\n"
        << "snapshot_key=1:space,2:status,3:logcat_flush,4:state_read,5:state_flush,6:rename\n"
        << "last_read_ms=" << s->read_ms.load() << " last_write_ms=" << s->write_ms.load() << "\n"
        << "read_bytes=" << s->read_bytes.load() << " written_bytes=" << s->written_bytes.load() << "\n"
        << "dropped_records=" << s->dropped_records.load() << " sequence_gaps=" << s->sequence_gaps.load()
        << " overruns=" << s->overruns.load() << "\n"
        << "reader_errno=" << s->reader_errno.load() << " writer_errno=" << s->writer_errno.load() << "\n"
        << "kernel_wait_status=" << kernel_exit << " logcat_wait_status=" << logcat_exit << "\n";
}

void Reap(pid_t& pid, int& status) {
    if (pid > 0 && waitpid(pid, &status, WNOHANG) == pid) pid = -1;
}

}  // namespace

int main() {
    umask(0077);
    // Never write into the empty rootfs mount point: only real metadata survives reboot.
    for (unsigned attempt = 0; attempt < 30 && !MetadataIsMounted(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!MetadataIsMounted()) {
        fprintf(stderr, "laurel_boot_logger: metadata is not mounted\n");
        return 1;
    }

    const std::string directory = CreateBootDirectory();
    if (directory.empty()) return 1;

    void* memory = mmap(nullptr, sizeof(CaptureStatus), PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return 1;
    auto* status = new (memory) CaptureStatus;
    pid_t kernel = fork();
    if (kernel == 0) { CaptureKernelLog(directory, status); _exit(0); }
    if (kernel < 0) { fprintf(stderr, "kernel collector fork: %s\n", strerror(errno)); return 1; }
    pid_t logcat = StartLogcat(directory + "/logcat.txt");
    if (logcat < 0) fprintf(stderr, "laurel_boot_logger: logcat fork failed: %s\n", strerror(errno));
    int kernel_exit = -1, logcat_exit = -1;
    pid_t snapshot = -1;
    int snapshot_exit = -1;
    uint64_t snapshot_started = 0;
    unsigned sequence = 0;
    const auto deadline = std::chrono::steady_clock::now() + kCaptureDuration;
    while (std::chrono::steady_clock::now() < deadline) {
        Reap(kernel, kernel_exit);
        Reap(logcat, logcat_exit);
        Reap(snapshot, snapshot_exit);
        if (snapshot_exit == (2 << 8)) break;
        if (snapshot > 0 && MonotonicMs() - snapshot_started > 2000) {
            // SIGKILL does not interrupt an uninterruptible kernel I/O wait.
            // Do not spawn more workers until this one is actually reaped.
            kill(snapshot, SIGKILL);
            fprintf(stderr, "laurel_boot_logger: snapshot worker blocked at phase %u; kernel collector independent\n",
                    status->snapshot_phase.load());
        }
        if (snapshot < 0) {
            snapshot_started = MonotonicMs();
            const unsigned current = sequence++;
            snapshot = fork();
            if (snapshot < 0) fprintf(stderr, "laurel_boot_logger: snapshot fork failed: %s\n", strerror(errno));
            if (snapshot == 0) {
                status->snapshot_phase = 1;
                struct statvfs space {};
                if (statvfs("/metadata", &space) < 0 ||
                    static_cast<uint64_t>(space.f_bavail) * space.f_frsize < kReserveBytes) {
                    fprintf(stderr, "laurel_boot_logger: metadata reserve reached\n");
                    if (kernel > 0) kill(kernel, SIGTERM);
                    if (logcat > 0) kill(logcat, SIGTERM);
                    _exit(2);
                }
                if (current == 0) {
                    const std::string info = directory + "/boot-info.txt";
                    AppendFile(info, "cmdline", "/proc/cmdline");
                    AppendFile(info, "kernel", "/proc/version");
                    AppendFile(info, "build identity", "/vendor/build.prop");
                }
                status->snapshot_phase = 2;
                WriteStatus(directory + "/collector-status.txt", status, kernel_exit, logcat_exit);
                // Logcat is independent; flush only its current file, not all metadata.
                status->snapshot_phase = 3;
                const int log_fd = open((directory + "/logcat.txt").c_str(), O_WRONLY | O_CLOEXEC);
                if (log_fd >= 0) { fdatasync(log_fd); close(log_fd); }
                // Commit through rename: a blocked/partial snapshot leaves the last complete one intact.
                const std::string tmp = directory + "/state.pending";
                status->snapshot_phase = 4;
                WriteSnapshot(tmp, current);
                status->snapshot_phase = 5;
                const int state_fd = open(tmp.c_str(), O_WRONLY | O_CLOEXEC);
                if (state_fd >= 0) {
                    struct stat st {};
                    if (fstat(state_fd, &st) == 0 && st.st_size > 48 * 1024) ftruncate(state_fd, 48 * 1024);
                    fdatasync(state_fd);
                    close(state_fd);
                }
                const int status_fd = open((directory + "/collector-status.txt").c_str(), O_WRONLY | O_CLOEXEC);
                if (status_fd >= 0) { fdatasync(status_fd); close(status_fd); }
                status->snapshot_phase = 6;
                rename(tmp.c_str(), (directory + (current == 0 ? "/initial-state.log" : "/state.log")).c_str());
                _exit(0);
            }
        }
        std::this_thread::sleep_for(kSnapshotInterval);
    }
    // Never join a collector stuck in kernel I/O. Init reaps remaining children.
    for (pid_t child : {kernel, logcat, snapshot}) if (child > 0) kill(child, SIGTERM);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    Reap(kernel, kernel_exit);
    Reap(logcat, logcat_exit);
    Reap(snapshot, snapshot_exit);
    for (pid_t child : {kernel, logcat, snapshot}) if (child > 0) kill(child, SIGKILL);
    return 0;
}
