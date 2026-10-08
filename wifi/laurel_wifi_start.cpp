// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <android-base/file.h>
#include <modprobe/modprobe.h>
#include <endian.h>
#include <linux/qrtr.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <string>
#include <vector>

using namespace std::chrono_literals;

// Subscribe once and wait for a real server announcement. A service PID being
// alive or a successful ctl.start write does not establish QRTR readiness.
static bool WaitForTqftp() {
    android::base::unique_fd fd(socket(AF_QIPCRTR, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if (fd.get() < 0) { PLOG(ERROR) << "QRTR socket"; return false; }
    sockaddr_qrtr address{};
    socklen_t length = sizeof(address);
    if (getsockname(fd.get(), reinterpret_cast<sockaddr*>(&address), &length) < 0 ||
        length != sizeof(address) || address.sq_family != AF_QIPCRTR) {
        PLOG(ERROR) << "QRTR local address";
        return false;
    }
    const auto local_node = address.sq_node;
    address.sq_port = QRTR_PORT_CTRL;
    qrtr_ctrl_pkt request{};
    request.cmd = htole32(QRTR_TYPE_NEW_LOOKUP);
    request.server.service = htole32(4096);
    if (sendto(fd.get(), &request, sizeof(request), 0,
               reinterpret_cast<sockaddr*>(&address), sizeof(address)) != static_cast<ssize_t>(sizeof(request))) {
        PLOG(ERROR) << "QRTR lookup";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd wait{fd.get(), POLLIN, 0};
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) break;
        const int result = poll(&wait, 1, static_cast<int>(remaining));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0 || !(wait.revents & POLLIN)) break;
        qrtr_ctrl_pkt packet{};
        const ssize_t count = recv(fd.get(), &packet, sizeof(packet), MSG_DONTWAIT);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count != static_cast<ssize_t>(sizeof(packet))) continue;
        if (le32toh(packet.cmd) == QRTR_TYPE_NEW_SERVER &&
            le32toh(packet.server.service) == 4096 &&
            (le32toh(packet.server.instance) & 0xff) == 1 &&
            le32toh(packet.server.node) == local_node && le32toh(packet.server.port)) {
            LOG(INFO) << "TQFTP QRTR service 4096 ready on local node " << local_node;
            return true;
        }
    }
    LOG(ERROR) << "TQFTP readiness timeout after 10 seconds; MPSS left stopped";
    return false;
}

int main(int, char** argv) {
    android::base::InitLogging(argv, android::base::KernelLogger);
    const std::string stage = android::base::GetProperty("ro.vendor.laurel.wifi.stage", "qmi-only");
    LOG(INFO) << "Laurel radio stage=" << stage;
    if (stage == "off") return 0;
    if (stage != "mpss" && stage != "qmi-only" && stage != "full") {
        LOG(ERROR) << "Invalid radio stage; radio left stopped";
        return 1;
    }
    if (access("/mnt/vendor/laurel_firmware/image/modem.mdt", R_OK) != 0) {
        PLOG(ERROR) << "Active-slot modem firmware unavailable; radio left stopped";
        return 1;
    }
    if (!android::base::WaitForProperty("vendor.dlkm.modules.ready", "true", 10s)) {
        LOG(ERROR) << "Mainline module loader not ready";
        return 1;
    }
    if (stage != "mpss") {
        // Do not accept an already-probed module with different parameters.
        // A stale modules.load would otherwise silently bypass QMI-only isolation.
        if (access("/sys/module/ath10k_snoc", F_OK) == 0) {
            std::string current;
            if (!android::base::ReadFileToString("/sys/module/ath10k_snoc/parameters/qmi_only", &current) ||
                current.empty() || current[0] != (stage == "qmi-only" ? 'Y' : 'N')) {
                LOG(ERROR) << "ath10k already loaded with an incompatible mode; rebuild/reboot";
                return 1;
            }
        }
        Modprobe modules(std::vector<std::string>{"/vendor/lib/modules"});
        if (!modules.LoadWithAliases("ath10k_snoc", true,
                                     stage == "qmi-only" ? "qmi_only=1" : "qmi_only=0")) {
            LOG(ERROR) << "Cannot load ath10k SNOC with diagnostic parameters";
            return 1;
        }
        // cfg80211/nl80211 only become available after the modules load.
        // Wificond may have started earlier and cached failed family discovery.
        if (stage == "full" &&
            !android::base::SetProperty("vendor.laurel.wifi.netlink_ready", "1")) {
            LOG(ERROR) << "Cannot publish nl80211 readiness";
            return 1;
        }
    }
    if (!android::base::SetProperty("ctl.start", "vendor.laurel-tqftpserv") || !WaitForTqftp()) return 1;
    // RMTFS publishes service 14 before its existing -s path boots MPSS.
    if (!android::base::SetProperty("ctl.start", "vendor.laurel-rmtfs")) {
        LOG(ERROR) << "Cannot start RMTFS; MPSS left stopped";
        return 1;
    }
    LOG(INFO) << "Radio startup requested; inspect FW_READY/CE logs for completion";
    return 0;
}
