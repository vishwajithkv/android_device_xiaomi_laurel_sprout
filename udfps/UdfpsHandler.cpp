/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "UdfpsHandler.laurel_sprout"

#include "UdfpsHandler.h"

#include <android-base/logging.h>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <poll.h>
#include <stdint.h>
#include <thread>
#include <unistd.h>

#define COMMAND_NIT 10
#define PARAM_NIT_FOD 1
#define PARAM_NIT_NONE 0

static constexpr auto kFodDisableDelay = std::chrono::milliseconds(1000);

template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

static const char* kFodUiPaths[] = {
        "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/fod_ui",
        "/sys/devices/platform/soc/soc:qcom,dsi-display/fod_ui",
};

static const char* kFodStatusPath = "/sys/class/touch/tp_dev/fod_status";

static bool readBool(int fd) {
    char c;
    int rc;

    rc = lseek(fd, 0, SEEK_SET);
    if (rc) {
        LOG(ERROR) << "failed to seek fd, err: " << rc;
        return false;
    }

    rc = read(fd, &c, sizeof(char));
    if (rc != 1) {
        LOG(ERROR) << "failed to read bool from fd, err: " << rc;
        return false;
    }

    return c != '0';
}

class LaurelSproutUdfpsHandler : public UdfpsHandler {
  public:
    void init(fingerprint_device_t* device) {
        mDevice = device;

        std::thread([this]() {
            int fd = -1;
            for (auto& path : kFodUiPaths) {
                fd = open(path, O_RDONLY);
                if (fd >= 0) {
                    break;
                }
            }

            if (fd < 0) {
                LOG(ERROR) << "failed to open fod_ui, err: " << fd;
                return;
            }

            struct pollfd fodUiPoll = {
                    .fd = fd,
                    .events = POLLERR | POLLPRI,
                    .revents = 0,
            };

            while (true) {
                int rc = poll(&fodUiPoll, 1, -1);
                if (rc < 0) {
                    LOG(ERROR) << "failed to poll fod_ui, err: " << rc;
                    continue;
                }

                const bool requested = readBool(fd);
                std::lock_guard<std::mutex> lock(mFodMutex);
                mFodUiRequested = requested;

                if (requested) {
                    ++mStateGeneration;
                    setFodStateLocked(true);
                } else if (!mFingerDown) {
                    scheduleDisableLocked();
                }
            }
        }).detach();
    }

    void onFingerDown(uint32_t /*x*/, uint32_t /*y*/, float /*minor*/, float /*major*/) {
        std::lock_guard<std::mutex> lock(mFodMutex);
        mFingerDown = true;
        mIgnoreVendorWait = false;
        ++mStateGeneration;
        setFodStateLocked(true);
    }

    void onFingerUp() {
        std::lock_guard<std::mutex> lock(mFodMutex);
        mFingerDown = false;
        if (!mFodUiRequested) {
            scheduleDisableLocked();
        }
    }

    void onAcquired(int32_t /*result*/, int32_t vendorCode) {
        if (vendorCode != 23) {
            return;
        }

        /*
         * Goodix uses vendor code 23 while waiting for another capture.
         * Keep FOD active across enrollment samples instead of interpreting
         * every successful sample as the end of the operation.
         */
        std::lock_guard<std::mutex> lock(mFodMutex);
        // Goodix may send this after an authentication result; wait for a new finger-down.
        if (mIgnoreVendorWait) {
            return;
        }

        ++mStateGeneration;
        setFodStateLocked(true);
    }

    void onAuthenticationSucceeded() override {
        finishAuthentication();
    }

    void onAuthenticationFailed() override {
        finishAuthentication();
    }

    void cancel() {
        std::lock_guard<std::mutex> lock(mFodMutex);
        mFingerDown = false;
        mFodUiRequested = false;
        mIgnoreVendorWait = true;
        ++mStateGeneration;
        setFodStateLocked(false);
    }

  private:
    void finishAuthentication() {
        std::lock_guard<std::mutex> lock(mFodMutex);
        mFingerDown = false;
        mIgnoreVendorWait = true;
        ++mStateGeneration;
        setFodStateLocked(false);
    }

    void scheduleDisableLocked() {
        const uint64_t generation = ++mStateGeneration;

        std::thread([this, generation]() {
            std::this_thread::sleep_for(kFodDisableDelay);

            std::lock_guard<std::mutex> lock(mFodMutex);
            if (generation == mStateGeneration && !mFingerDown && !mFodUiRequested) {
                setFodStateLocked(false);
            }
        }).detach();
    }

    void setGoodixFodStateLocked(bool enabled) {
        if (mGoodixFodEnabled == enabled) {
            return;
        }

        mDevice->extCmd(mDevice, COMMAND_NIT, enabled ? PARAM_NIT_FOD : PARAM_NIT_NONE);
        mGoodixFodEnabled = enabled;
    }

    void setTouchFodModeLocked(bool enabled) {
        if (mTouchFodEnabled == enabled) {
            return;
        }

        set(kFodStatusPath, enabled ? 1 : 0);
        mTouchFodEnabled = enabled;
    }

    void setFodStateLocked(bool enabled) {
        setGoodixFodStateLocked(enabled);
        setTouchFodModeLocked(enabled);
    }

    fingerprint_device_t* mDevice;
    std::mutex mFodMutex;
    bool mFodUiRequested = false;
    bool mFingerDown = false;
    bool mGoodixFodEnabled = false;
    bool mTouchFodEnabled = false;
    bool mIgnoreVendorWait = false;
    uint64_t mStateGeneration = 0;
};

static UdfpsHandler* create() {
    return new LaurelSproutUdfpsHandler();
}

static void destroy(UdfpsHandler* handler) {
    delete handler;
}

extern "C" UdfpsHandlerFactory UDFPS_HANDLER_FACTORY = {
        .create = create,
        .destroy = destroy,
};
