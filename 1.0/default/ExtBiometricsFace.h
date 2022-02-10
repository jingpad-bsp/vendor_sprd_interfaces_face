// FIXME: your file license if you have one

#pragma once

#include <log/log.h>
#include <android/log.h>
#include <hardware/hardware.h>
#include <hardware/face.h>
#include <vendor/sprd/hardware/face/1.0/IExtBiometricsFace.h>
#include <vendor/sprd/hardware/face/1.0/IExtBiometricsFaceClientCallback.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/AMessage.h>

namespace vendor {
namespace sprd {
namespace hardware {
namespace face {
namespace V1_0 {
namespace implementation {

using ::android::hardware::biometrics::face::V1_0::IBiometricsFace;
using ::android::hardware::biometrics::face::V1_0::IBiometricsFaceClientCallback;
using ::android::hardware::biometrics::face::V1_0::Status;
using ::android::hardware::biometrics::face::V1_0::Feature;
using ::android::hardware::biometrics::face::V1_0::FaceError;
using ::android::hardware::biometrics::face::V1_0::FaceAcquiredInfo;
using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;
using ::vendor::sprd::hardware::face::V1_0::IExtBiometricsFace;
using ::vendor::sprd::hardware::face::V1_0::IExtBiometricsFaceClientCallback;
using ::android::AHandler;
using ::android::ALooper;
using ::android::AMessage;

struct FaceHandler : public AHandler {
    FaceHandler() {}
    ~FaceHandler() {}

    void onMessageReceived(const sp<AMessage> &msg);

private:
    DISALLOW_EVIL_CONSTRUCTORS(FaceHandler);
};

struct ExtBiometricsFace : public IExtBiometricsFace {
public:
    ExtBiometricsFace();
    ~ExtBiometricsFace();

    // Method to wrap legacy HAL with ExtBiometricsFace class
    static IExtBiometricsFace* getInstance();
    face_device_t* getDevice();

    // Methods from ::android::hardware::biometrics::face::V1_0::IBiometricsFace follow.
    Return<void> setCallback(const sp<IBiometricsFaceClientCallback>& clientCallback, setCallback_cb _hidl_cb) override;
    Return<Status> setActiveUser(int32_t userId, const hidl_string& storePath) override;
    Return<void> generateChallenge(uint32_t challengeTimeoutSec, generateChallenge_cb _hidl_cb) override;
    Return<Status> enroll(const hidl_vec<uint8_t>& hat, uint32_t timeoutSec, const hidl_vec<Feature>& disabledFeatures) override;
    Return<Status> revokeChallenge() override;
    Return<Status> setFeature(Feature feature, bool enabled, const hidl_vec<uint8_t>& hat, uint32_t faceId) override;
    Return<void> getFeature(Feature feature, uint32_t faceId, getFeature_cb _hidl_cb) override;
    Return<void> getAuthenticatorId(getAuthenticatorId_cb _hidl_cb) override;
    Return<Status> cancel() override;
    Return<Status> enumerate() override;
    Return<Status> remove(uint32_t faceId) override;
    Return<Status> authenticate(uint64_t operationId) override;
    Return<Status> userActivity() override;
    Return<Status> resetLockout(const hidl_vec<uint8_t>& hat) override;

    // Methods from ::vendor::sprd::hardware::face::V1_0::IExtBiometricsFace follow.
    Return<Status> doEnrollProcess(int64_t addr, const hidl_vec<int32_t>& info, const hidl_vec<int8_t>& byteInfo) override;
    Return<Status> doAuthenticateProcess(int64_t main, int64_t sub, int64_t otp, const hidl_vec<int32_t>& info, const hidl_vec<int8_t>& byteInfo) override;
    Return<Status> updateLivenessMode(int32_t value, int32_t userId) override;

private:
    static face_device_t* openHal();
    static void notify(const face_msg_t *msg); /* Static callback for legacy HAL implementation */
    static Return<Status> ErrorFilter(int32_t error);
    static FaceError VendorErrorFilter(int32_t error, int32_t* vendorCode);
    static FaceAcquiredInfo VendorAcquiredFilter(int32_t error, int32_t* vendorCode);
    static ExtBiometricsFace* sInstance;

    std::mutex mClientCallbackMutex;
    sp<IBiometricsFaceClientCallback> mClientCallback;
    sp<IExtBiometricsFaceClientCallback> mExtClientCallback;
    int32_t mUserId;
    face_device_t *mDevice;
    sp<ALooper> mLooper;
    sp<FaceHandler> mHandler;
    bool mCancelled;
    std::mutex mCancelledMutex;

    friend struct FaceHandler;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace face
}  // namespace hardware
}  // namespace sprd
}  // namespace vendor
