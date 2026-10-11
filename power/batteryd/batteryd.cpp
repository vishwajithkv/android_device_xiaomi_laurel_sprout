// SPDX-License-Identifier: GPL-2.0-only
// Open SOC policy for PMI632 QG. Stock profile data is in profiles.h.
// This service has no charger-control interface. Positive current is charging.
#include "qcom_qg.h"
#include "profiles.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <log/log.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static_assert(sizeof(qcom_qg_snapshot) == 128, "QG snapshot ABI changed");
static_assert(sizeof(qcom_qg_estimate) == 56, "QG estimate ABI changed");

static constexpr int kStatusCharging = 1, kStatusFull = 4, kHealthGood = 1;
static constexpr double kNcPerUah = 3600000.0;

// Reboot continuity belongs to kernel QG SDAM/RTC, shared with recovery.
static const Profile &profileFor(unsigned ohm) {
    for (const auto *profile : profiles) if (profile->ohm == ohm) return *profile;
    return p0;
}
static double lerp(double a, double b, double f) { return a + (b - a) * f; }
static double socColumn(const Table &t, size_t col, int voltage) {
    if (voltage >= t.uv[col]) return t.soc[0];
    for (size_t row = 1; row < t.rows; ++row) {
        int high = t.uv[(row - 1) * t.cols + col], low = t.uv[row * t.cols + col];
        if (voltage >= low && high > low)
            return lerp(t.soc[row], t.soc[row - 1], static_cast<double>(voltage - low) / (high - low));
    }
    return t.soc[t.rows - 1];
}
static double ocvSoc(const Table &t, int temp, int voltage) {
    if (temp <= t.temps[0] * 10) return socColumn(t, 0, voltage);
    for (size_t col = 1; col < t.cols; ++col)
        if (temp < t.temps[col] * 10)
            return lerp(socColumn(t, col - 1, voltage), socColumn(t, col, voltage),
                        static_cast<double>(temp - t.temps[col - 1] * 10) /
                        ((t.temps[col] - t.temps[col - 1]) * 10));
    return socColumn(t, t.cols - 1, voltage);
}
static double temperatureScale(const Profile &p, int temp) {
    const Table &t = p.discharge;
    double fcc = p.fcc[0];
    for (size_t col = 1; col < t.cols; ++col) {
        if (temp < t.temps[col] * 10) {
            fcc = lerp(p.fcc[col - 1], p.fcc[col], std::clamp(
                static_cast<double>(temp - t.temps[col - 1] * 10) /
                ((t.temps[col] - t.temps[col - 1]) * 10), 0.0, 1.0));
            break;
        }
        fcc = p.fcc[col];
    }
    return fcc / p.fcc[2]; // Stock discharge FCC at 25 C.
}

class Gauge {
    bool initialised_ = false, historyValid_ = false, full_ = false;
    bool learning_ = false;
    uint64_t generation_ = 0, previousSamples_ = 0, lastStep_ = 0;
    uint64_t fullSince_ = 0, restSince_ = 0, taperVoltageMs_ = 0;
    uint32_t gaps_ = 0;
    int64_t previousCharge_ = 0, learningCharge_ = 0;
    double soc_ = 0, displayed_ = 0, fullUah_ = 0, dischargeUah_ = 0, learningSoc_ = 0;
    int cycles_ = 0;
public:
    qcom_qg_estimate update(const qcom_qg_snapshot &s) {
        const auto &p = profileFor(s.profile_ohm);
        const bool present = s.flags & QCOM_QG_PRESENT;
        const bool freshOcv = s.flags & QCOM_QG_OCV_VALID;
        const bool fifo = s.flags & QCOM_QG_FIFO_VALID;
        const bool normalTemp = s.temperature_decic >= 100 && s.temperature_decic <= 440;
        if (!initialised_ || generation_ != s.generation) {
            // PON OCV is a seed, not permission to infer SOC continuously
            // from charging/loaded terminal voltage.
            soc_ = ocvSoc(p.discharge, s.temperature_decic, freshOcv ? s.ocv_uv : s.voltage_uv);
            displayed_ = soc_;
            fullUah_ = s.saved_full_uah > 0 ? s.saved_full_uah : s.design_uah;
            cycles_ = std::max(0, s.saved_cycles);
            historyValid_ = s.saved_full_uah > 0 && s.saved_cycles >= 0;
            dischargeUah_ = 0;
            if (s.accepted_soc_basis_points >= 0) {
                // Kernel seeds both estimator and display from validated
                // SDAM, or its last accepted estimate after service restart.
                soc_ = displayed_ = s.accepted_soc_basis_points;
                ALOGI("Kernel SOC seed %.2f%% (%s)", displayed_ / 100,
                      s.flags & QCOM_QG_SOC_RESTORED ? "SDAM" : "live estimate");
            }
            generation_ = s.generation;
            previousCharge_ = s.charge_nc;
            previousSamples_ = s.sample_ms;
            gaps_ = s.gaps;
            fullSince_ = restSince_ = taperVoltageMs_ = 0;
            full_ = (s.flags & QCOM_QG_FULL_HELD) && s.input_online &&
                    normalTemp && s.health == kHealthGood;
            learning_ = false;
            lastStep_ = s.timestamp_ms;
            initialised_ = present;
        } else {
            const double delta = static_cast<double>(s.charge_nc - previousCharge_) / kNcPerUah;
            // The kernel integrates only actual FIFO durations. Even a batch
            // next to a gap contributes its measured charge, never a guessed
            // current extrapolated over the whole elapsed wall time.
            if (fifo && s.sample_ms > previousSamples_) {
                soc_ += delta * 10000 / (fullUah_ * temperatureScale(p, s.temperature_decic));
                if (delta < 0 && s.gaps == gaps_) {
                    dischargeUah_ -= delta;
                    while (dischargeUah_ >= fullUah_ && cycles_ < INT32_MAX) {
                        dischargeUah_ -= fullUah_;
                        ++cycles_;
                    }
                }
            }
            if (s.gaps != gaps_ || !normalTemp || !present) learning_ = false;
            previousCharge_ = s.charge_nc;
            previousSamples_ = s.sample_ms;
            gaps_ = s.gaps;
        }
        const bool atRest = present && !s.input_online && std::abs(s.current_ua) <= 20000;
        if (atRest) {
            if (!restSince_) restSince_ = s.timestamp_ms;
        } else restSince_ = 0;
        // Only a real resting hardware OCV can correct drift, and only after
        // five minutes without charging/load. Loaded voltage is never used.
        if (freshOcv && restSince_ && s.timestamp_ms - restSince_ >= 300000) {
            const double target = ocvSoc(p.discharge, s.temperature_decic, s.ocv_uv);
            soc_ += std::clamp(target - soc_, -1.0, 1.0); // <= 0.01% per 2 s.
        }
        // Remember the normal-voltage taper preceding termination. Once
        // charging stops, VBAT relaxes; requiring 4.39 V for a whole minute
        // after termination would reject a legitimately completed charge.
        if (s.input_online && normalTemp && s.charger_state == 4 &&
            s.current_ua > 0 && s.voltage_uv >= 4390000 && s.float_uv >= 4390000)
            taperVoltageMs_ = s.timestamp_ms;
        if (!s.input_online || !normalTemp) taperVoltageMs_ = 0;
        const bool terminated = s.charger_state == 5 && s.status == kStatusFull;
        const bool qualifies = present && s.input_online && s.charger_enabled && terminated &&
            s.health == kHealthGood && normalTemp && s.float_uv >= 4390000 &&
            s.voltage_uv >= s.float_uv - 100000 && std::abs(s.current_ua) <= 330000 &&
            ((soc_ >= 9900) || (taperVoltageMs_ &&
             s.timestamp_ms - taperVoltageMs_ <= 300000 && fifo && s.sample_ms >= 51200 && soc_ >= 9000));
        // A measured taper followed by sustained normal-temperature hardware
        // termination is an independent full endpoint. The PON OCV seed may
        // be a few percent low; never restart charging merely to raise it.
        // Display correction still obeys the existing 1% / 20 s bound.
        if (qualifies) {
            if (!fullSince_) fullSince_ = s.timestamp_ms;
            if (!full_ && s.timestamp_ms - fullSince_ >= 60000) {
                full_ = true;
                if (learning_) {
                    const double measured = static_cast<double>(s.charge_nc - learningCharge_) /
                        kNcPerUah * 10000 / (10000 - learningSoc_);
                    // Reject implausible sessions; adapt at most 10% per
                    // qualified cycle and preserve stock SDAM unchanged.
                    if (measured >= s.design_uah * 0.5 && measured <= s.design_uah * 1.5) {
                        fullUah_ += std::clamp((measured - fullUah_) * 0.25,
                                             -fullUah_ * 0.1, fullUah_ * 0.1);
                        historyValid_ = true;
                    }
                    learning_ = false;
                }
                soc_ = 10000;
            }
        } else fullSince_ = 0;
        // A new active high-current charge is a recharge session, not the
        // prior completed session. Thermal pauses remain genuine pauses.
        if (!s.input_online || !normalTemp || s.health != kHealthGood || soc_ < 9900 ||
            s.charger_state < 3 || s.charger_state > 5 ||
            (s.charger_state >= 1 && s.charger_state <= 4 && s.current_ua > 330000))
            full_ = false;
        // Begin learning from a low, independently measured rest-OCV anchor.
        if (!learning_ && freshOcv && normalTemp && fifo && s.input_online &&
            s.status == kStatusCharging && s.current_ua > 0 && soc_ <= 1500 &&
            std::abs(ocvSoc(p.discharge, s.temperature_decic, s.ocv_uv) - soc_) <= 200) {
            learning_ = true;
            learningSoc_ = soc_;
            learningCharge_ = s.charge_nc;
        }
        if (!s.input_online) learning_ = false;
        soc_ = std::clamp(soc_, 0.0, 10000.0);
        // UI steps are independent of ioctl/property polling frequency.
        // Downstream maint_soc keeps the first step away from full gradual.
        // The underlying coulomb estimate continues to move independently.
        const uint64_t stepMs = displayed_ >= 9900 && soc_ < displayed_ ? 120000 : 20000;
        if (s.timestamp_ms - lastStep_ >= stepMs) {
            const double target = full_ && s.input_online && soc_ >= 9900 ? 10000 : soc_;
            double step = std::clamp(target - displayed_, -100.0, 100.0);
            // Stock direction guard: never rise without input/charging
            // current; never fall during a healthy net charging interval.
            if (step > 0 && !full_ && (!s.input_online || s.current_ua <= 0)) step = 0;
            if (step < 0 && s.input_online && s.current_ua > 0) step = 0;
            displayed_ = std::clamp(displayed_ + step, 0.0, 10000.0);
            lastStep_ = s.timestamp_ms;
        }
        qcom_qg_estimate e{};
        e.version = QCOM_QG_ABI_VERSION;
        e.generation = s.generation; e.sequence = s.sequence;
        e.soc_basis_points = static_cast<int>(std::lround(displayed_));
        e.full_uah = static_cast<int>(std::lround(fullUah_)); e.cycles = cycles_;
        if (full_ && displayed_ >= 10000) e.flags |= QCOM_QG_FULL_QUALIFIED;
        if (historyValid_ && p.ohm) e.flags |= QCOM_QG_LEARNED_VALID;
        return e;
    }
};

int main() {
    Gauge gauge;
    int fd = -1;
    uint64_t lastLog = 0;
    for (;;) {
        if (fd < 0) fd = open("/dev/qcom-qg", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        if (fd >= 0) {
            qcom_qg_snapshot s{};
            if (!ioctl(fd, QCOM_QG_GET_SNAPSHOT, &s)) {
                if (s.version != QCOM_QG_ABI_VERSION || !(s.flags & QCOM_QG_PRESENT) || s.design_uah <= 0) {
                    ALOGW("QG ABI/presence/design not ready");
                } else {
                    auto e = gauge.update(s);
                    if (ioctl(fd, QCOM_QG_SUBMIT_ESTIMATE, &e) && errno != ESTALE)
                        ALOGE("Cannot submit QG estimate: %s", strerror(errno));
                    if (s.timestamp_ms - lastLog >= 30000) {
                        ALOGI("QG soc=%d.%02d full=%d cycles=%d I=%d V=%d state=%u nc=%lld samples=%llu gaps=%u flags=%u",
                            e.soc_basis_points / 100, e.soc_basis_points % 100, e.full_uah, e.cycles,
                            s.current_ua, s.voltage_uv, s.charger_state,
                            static_cast<long long>(s.charge_nc), static_cast<unsigned long long>(s.sample_ms),
                            s.gaps, e.flags);
                        lastLog = s.timestamp_ms;
                    }
                }
            } else {
                ALOGW("Cannot obtain QG snapshot: %s", strerror(errno));
                if (errno == ENODEV || errno == EBADF) { close(fd); fd = -1; }
            }
        }
        struct timespec interval {2, 0};
        while (nanosleep(&interval, &interval) && errno == EINTR) {}
    }
}
