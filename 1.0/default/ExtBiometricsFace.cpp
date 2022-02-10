// FIXME: your file license if you have one

#define LOG_TAG "vendor.sprd.hardware.face@1.0-service"

#include <hardware/hw_auth_token.h>

#include <hardware/hardware.h>
#include <hardware/face.h>
#include <cutils/properties.h>
#include <inttypes.h>
#include <unistd.h>
#include "ExtBiometricsFace.h"

namespace vendor {
namespace sprd {
namespace hardware {
namespace face {
namespace V1_0 {
namespace implementation {

enum {
    ENROLL_REQUEST,
    AUTH_REQUEST,
    ENUMERATE_REQUEST,
    REMOVE_REQUEST,
    CANCEL_REQUEST,
    ENROLL_PROCESS_REQUEST,
    AUTH_PROCESS_REQUEST,
};

#define MAX_FEATURES 2
#define ACTIVE_USER_STORE_PATH_MIN_LEN 2

static hw_auth_token_t sToken;
static uint32_t sDisabledFeature[MAX_FEATURES];
static bool sIsAlgoInitialized = false;

void FaceHandler::onMessageReceived(const sp<AMessage> &msg){
    face_device_t* device = static_cast<ExtBiometricsFace*>(ExtBiometricsFace::getInstance())->getDevice();
    switch (msg->what()) {
    case ENROLL_REQUEST:
    {
        ALOGD("onMessageReceived ENROLL_REQUEST");
        int32_t timeoutSec = 0;
        msg->findInt32("timeoutSec", &timeoutSec);
        size_t size = 0;
        msg->findSize("disabledFeaturesSize", &size);
        device->enroll(device, &sToken, timeoutSec, sDisabledFeature, size);
        sIsAlgoInitialized = true;
        break;
    }
    case AUTH_REQUEST:
    {
        ALOGD("onMessageReceived AUTH_REQUEST");
        int64_t operationId = 0;
        msg->findInt64("operationId", &operationId);
        device->authenticate(device, operationId);
        sIsAlgoInitialized = true;
        break;
    }
    case ENUMERATE_REQUEST:
    {
        ALOGD("onMessageReceived ENUMERATE_REQUEST");
        device->enumerate(device);
        break;
    }
    case REMOVE_REQUEST:
    {
        ALOGD("onMessageReceived REMOVE_REQUEST");
        int32_t faceId = 0;
        msg->findInt32("faceId", &faceId);
        device->remove(device, faceId);
        break;
    }
    case CANCEL_REQUEST:
    {
        ALOGD("onMessageReceived CANCEL_REQUEST");
        device->cancel(device);
        break;
    }
    case ENROLL_PROCESS_REQUEST:
    {
        ALOGD("onMessageReceived ENROLL_PROCESS_REQUEST");
        int64_t addr = 0;
        size_t infoSize = 0;
        int32_t* pInfo = NULL;
        size_t byteInfoSize = 0;
        int8_t* pByteInfo = NULL;
        msg->findInt64("addr", &addr);
        msg->findSize("infoSize", &infoSize);
        msg->findPointer("pInfo", (void **)&pInfo);
        msg->findSize("byteInfoSize", &byteInfoSize);
        msg->findPointer("pByteInfo", (void **)&pByteInfo);
        {
            std::lock_guard<std::mutex> lock(static_cast<ExtBiometricsFace*>(ExtBiometricsFace::getInstance())->mCancelledMutex);
            if(static_cast<ExtBiometricsFace*>(ExtBiometricsFace::getInstance())->mCancelled) {
                free(pInfo);
                free(pByteInfo);
                return;
            }
        }
        if(!sIsAlgoInitialized) {
            ALOGD("doEnrollProcess ignore as not initialized");
            static_cast<ExtBiometricsFace*>(ExtBiometricsFace::getInstance())->mExtClientCallback->onEnrollProcessed(reinterpret_cast<uint64_t>(device), addr);
        } else {
            device->do_enroll_process(device, addr, pInfo, infoSize, pByteInfo, byteInfoSize);
        }
        free(pInfo);
        free(pByteInfo);
        break;
    }
    case AUTH_PROCESS_REQUEST:
    {
        ALOGD("onMessageReceived AUTH_PROCESS_REQUEST");
        int64_t main = 0;
        int64_t sub = 0;
        int64_t otp = 0;
        size_t infoSize = 0;
        int32_t* pInfo = NULL;
        size_t byteInfoSize = 0;
        int8_t* pByteInfo = NULL;
        msg->findInt64("main", &main);
        msg->findInt64("sub", &sub);
        msg->findInt64("otp", &otp);
        msg->findSize("infoSize", &infoSize);
        msg->findPointer("pInfo", (void **)&pInfo);
        msg->findSize("byteInfoSize", &byteInfoSize);
        msg->findPointer("pByteInfo", (void **)&pByteInfo);
        {
            std::lock_guard<std::mutex> lock(static_cast<ExtBiometricsFace*>(ExtBiometricsFace::getInstance())->mCancelledMutex);
            if(static_cast<ExtBiometricsFace*>(ExtBiometricsFace::getInstance())->mCancelled) {
                free(pInfo);
                free(pByteInfo);
                return;
            }
        }
        if(!sIsAlgoInitialized) {
            ALOGD("doAuthenticateProcess ignore as not initialized");
            static_cast<ExtBiometricsFace*>(ExtBiometricsFace::getInstance())->mExtClientCallback->onAuthProcessed(reinterpret_cast<uint64_t>(device), main, sub);
        } else {
            device->do_authenticate_process(device, main, sub, otp, pInfo, infoSize, pByteInfo, byteInfoSize);
        }
        free(pInfo);
        free(pByteInfo);
        break;
    }
    default:
        break;
    }
}

// Supported face HAL version
static const uint16_t kVersion = HARDWARE_MODULE_API_VERSION(1, 0);

ExtBiometricsFace *ExtBiometricsFace::sInstance = nullptr;

ExtBiometricsFace::ExtBiometricsFace() : mClientCallback(nullptr), mExtClientCallback(nullptr), mUserId(-1), mDevice(nullptr), mCancelled(false) {
    sInstance = this; // keep track of the most recent instance
    mDevice = openHal();
    if (!mDevice) {
        ALOGE("Can't open HAL module");
    } else {
        mLooper = new ALooper;
        mLooper->setName("FaceRequestLooper");
        mHandler = new FaceHandler;
        mLooper->registerHandler(mHandler);
        mLooper->start(false/* runOnCallingThread */, false/* canCallJava *//*, ANDROID_PRIORITY_FOREGROUND*/);
    }
}

ExtBiometricsFace::~ExtBiometricsFace() {
    ALOGD("~BiometricsFace()");
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return;
    }
    int err;
    if (0 != (err = mDevice->common.close(
            reinterpret_cast<hw_device_t*>(mDevice)))) {
        ALOGE("Can't close face module, error: %d", err);
        return;
    }
    mDevice = nullptr;
}

face_device_t* ExtBiometricsFace::getDevice() {
    return mDevice;
}

Return<Status> ExtBiometricsFace::ErrorFilter(int32_t error) {
    switch(error) {
        case FACE_OK: return Status::OK;
        case FACE_ILLEGAL_ARGUMENT: return Status::ILLEGAL_ARGUMENT;
        case FACE_OPERATION_NOT_SUPPORTED: return Status::OPERATION_NOT_SUPPORTED;
        case FACE_NOT_ENROLLED: return Status::NOT_ENROLLED;
        default:
            ALOGE("An unknown error returned from face vendor library: %d", error);
            return Status::INTERNAL_ERROR;
    }
}

// Translate from errors returned by traditional HAL (see face.h) to
// HIDL-compliant FaceError.
FaceError ExtBiometricsFace::VendorErrorFilter(int32_t error,
            int32_t* vendorCode) {
    *vendorCode = 0;
    switch(error) {
        case FACE_ERROR_HW_UNAVAILABLE:
            return FaceError::HW_UNAVAILABLE;
        case FACE_ERROR_UNABLE_TO_PROCESS:
            return FaceError::UNABLE_TO_PROCESS;
        case FACE_ERROR_TIMEOUT:
            return FaceError::TIMEOUT;
        case FACE_ERROR_NO_SPACE:
            return FaceError::NO_SPACE;
        case FACE_ERROR_CANCELED:
            return FaceError::CANCELED;
        case FACE_ERROR_UNABLE_TO_REMOVE:
            return FaceError::UNABLE_TO_REMOVE;
        case FACE_ERROR_LOCKOUT:
            return FaceError::LOCKOUT;
        case FACE_ERROR_LOCKOUT_PERMANENT:
            return FaceError::LOCKOUT_PERMANENT;
        case FACE_ERROR_VERIFY_TOKEN_FAIL:
            *vendorCode = error - FACE_ERROR_VENDOR_BASE;
            return FaceError::UNABLE_TO_PROCESS;
        default:
            if (error >= FACE_ERROR_VENDOR_BASE) {
                // vendor specific code.
                *vendorCode = error - FACE_ERROR_VENDOR_BASE;
                return FaceError::VENDOR;
            }
    }
    ALOGE("Unknown error from face vendor library: %d", error);
    return FaceError::UNABLE_TO_PROCESS;
}

// Translate acquired messages returned by traditional HAL (see face.h)
// to HIDL-compliant FaceAcquiredInfo.
FaceAcquiredInfo ExtBiometricsFace::VendorAcquiredFilter(
        int32_t info, int32_t* vendorCode) {
    *vendorCode = 0;
    switch(info) {
        case FACE_ACQUIRED_GOOD:
            return FaceAcquiredInfo::GOOD;
        case FACE_ACQUIRED_INSUFFICIENT:
            return FaceAcquiredInfo::INSUFFICIENT;
        case FACE_ACQUIRED_TOO_BRIGHT:
            return FaceAcquiredInfo::TOO_BRIGHT;
        case FACE_ACQUIRED_TOO_DARK:
            return FaceAcquiredInfo::TOO_DARK;
        case FACE_ACQUIRED_TOO_CLOSE:
            return FaceAcquiredInfo::TOO_CLOSE;
        case FACE_ACQUIRED_TOO_FAR:
            return FaceAcquiredInfo::TOO_FAR;
        case FACE_ACQUIRED_FACE_TOO_HIGH:
            return FaceAcquiredInfo::FACE_TOO_HIGH;
        case FACE_ACQUIRED_FACE_TOO_LOW:
            return FaceAcquiredInfo::FACE_TOO_LOW;
        case FACE_ACQUIRED_FACE_TOO_RIGHT:
            return FaceAcquiredInfo::FACE_TOO_RIGHT;
        case FACE_ACQUIRED_FACE_TOO_LEFT:
            return FaceAcquiredInfo::FACE_TOO_LEFT;
        case FACE_ACQUIRED_POOR_GAZE:
            return FaceAcquiredInfo::POOR_GAZE;
        case FACE_ACQUIRED_NOT_DETECTED:
            return FaceAcquiredInfo::NOT_DETECTED;
        case FACE_ACQUIRED_TOO_MUCH_MOTION:
            return FaceAcquiredInfo::TOO_MUCH_MOTION;
        case FACE_ACQUIRED_RECALIBRATE:
            return FaceAcquiredInfo::RECALIBRATE;
        case FACE_ACQUIRED_TOO_DIFFERENT:
            return FaceAcquiredInfo::TOO_DIFFERENT;
        case FACE_ACQUIRED_TOO_SIMILAR:
            return FaceAcquiredInfo::TOO_SIMILAR;
        case FACE_ACQUIRED_PAN_TOO_EXTREME:
            return FaceAcquiredInfo::PAN_TOO_EXTREME;
        case FACE_ACQUIRED_TILT_TOO_EXTREME:
            return FaceAcquiredInfo::TILT_TOO_EXTREME;
        case FACE_ACQUIRED_ROLL_TOO_EXTREME:
            return FaceAcquiredInfo::ROLL_TOO_EXTREME;
        case FACE_ACQUIRED_FACE_OBSCURED:
            return FaceAcquiredInfo::FACE_OBSCURED;
        case FACE_ACQUIRED_START:
            return FaceAcquiredInfo::START;
        case FACE_ACQUIRED_SENSOR_DIRTY:
            return FaceAcquiredInfo::SENSOR_DIRTY;
        default:
            if (info >= FACE_ACQUIRED_VENDOR_BASE) {
                // vendor specific code.
                *vendorCode = info - FACE_ACQUIRED_VENDOR_BASE;
                return FaceAcquiredInfo::VENDOR;
            }
    }
    ALOGE("Unknown acquired msg from face vendor library: %d", info);
    return FaceAcquiredInfo::INSUFFICIENT;
}

// Methods from ::android::hardware::biometrics::face::V1_0::IBiometricsFace follow.
Return<void> ExtBiometricsFace::setCallback(const sp<IBiometricsFaceClientCallback>& clientCallback, setCallback_cb _hidl_cb) {
    ALOGD("setCallback");
    std::lock_guard<std::mutex> lock(mClientCallbackMutex);
    mClientCallback = clientCallback;
    mExtClientCallback = IExtBiometricsFaceClientCallback::castFrom(clientCallback);
    _hidl_cb({Status::OK, reinterpret_cast<uint64_t>(mDevice)});
    return Void();
}

Return<Status> ExtBiometricsFace::setActiveUser(int32_t userId, const hidl_string& storePath) {
    ALOGD("setActiveUser");
    if (storePath.size() >= PATH_MAX || storePath.size() < ACTIVE_USER_STORE_PATH_MIN_LEN) {
        ALOGE("Bad path length: %zd", storePath.size());
        return Status::INTERNAL_ERROR;
    }
    /*if (access(storePath.c_str(), W_OK)) {
        return Status::INTERNAL_ERROR;
    }*/

    return ErrorFilter(mDevice->set_active_group(mDevice, userId,
                                                    storePath.c_str()));
}

Return<void> ExtBiometricsFace::generateChallenge(uint32_t challengeTimeoutSec, generateChallenge_cb _hidl_cb) {
    ALOGD("generateChallenge challengeTimeoutSec:%d", challengeTimeoutSec);
    uint64_t challenge = 0;
    Status status = ErrorFilter(mDevice->pre_enroll(mDevice, challengeTimeoutSec, &challenge));
    _hidl_cb({status, challenge});
    return Void();
}

Return<Status> ExtBiometricsFace::enroll(const hidl_vec<uint8_t>& hat, uint32_t timeoutSec, const hidl_vec<Feature>& disabledFeatures) {
    ALOGD("enroll(timeoutSec=%d)\n", timeoutSec);
    const hw_auth_token_t* authToken =
        reinterpret_cast<const hw_auth_token_t*>(hat.data());
    uint32_t* p = (uint32_t*)disabledFeatures.data();
    size_t size = disabledFeatures.size();
    memcpy(&sToken, authToken, sizeof(hw_auth_token_t));
    for(size_t i = 0; i < size; i++) {
        sDisabledFeature[i] = p[i];
    }
    std::lock_guard<std::mutex> lock(mCancelledMutex);
    mCancelled = false;
    sp<AMessage> msg = new AMessage(ENROLL_REQUEST, mHandler);
    msg->setInt32("timeoutSec", (int32_t)timeoutSec);
    msg->setSize("disabledFeaturesSize", size);
    msg->post(0);
    return Status::OK;
    //return ErrorFilter(mDevice->enroll(mDevice, authToken, timeoutSec, (uint32_t*)disabledFeatures.data(), disabledFeatures.size()));
}

Return<Status> ExtBiometricsFace::revokeChallenge() {
    ALOGD("revokeChallenge");
    return ErrorFilter(mDevice->post_enroll(mDevice));
}

Return<Status> ExtBiometricsFace::setFeature(Feature feature, bool enabled, const hidl_vec<uint8_t>& hat, uint32_t faceId) {
    ALOGD("setFeature feature:%d enabled:%d", feature, enabled);
    const hw_auth_token_t* authToken =
        reinterpret_cast<const hw_auth_token_t*>(hat.data());
    return ErrorFilter(mDevice->set_feature(mDevice, (uint32_t)feature, enabled, authToken, faceId));
}

Return<void> ExtBiometricsFace::getFeature(Feature feature, uint32_t faceId, getFeature_cb _hidl_cb) {
    ALOGD("getFeature feature:%d", feature);
    bool result = true;
    Status status = ErrorFilter(mDevice->get_feature(mDevice, (uint32_t)feature, faceId, &result));
    _hidl_cb({status, result});
    return Void();
}

Return<void> ExtBiometricsFace::getAuthenticatorId(getAuthenticatorId_cb _hidl_cb) {
    ALOGD("getAuthenticatorId");
    uint64_t id = 0;
    Status status = ErrorFilter(mDevice->get_authenticator_id(mDevice, &id));
    _hidl_cb({status, id});
    return Void();
}

Return<Status> ExtBiometricsFace::cancel() {
    ALOGD("cancel");
    std::lock_guard<std::mutex> lock(mCancelledMutex);
    mCancelled = true;
    sp<AMessage> msg = new AMessage(CANCEL_REQUEST, mHandler);
    msg->post(0);
    return Status::OK;
    //return ErrorFilter(mDevice->cancel(mDevice));
}

Return<Status> ExtBiometricsFace::enumerate() {
    ALOGD("enumerate");
    sp<AMessage> msg = new AMessage(ENUMERATE_REQUEST, mHandler);
    msg->post(0);
    return Status::OK;
    //return ErrorFilter(mDevice->enumerate(mDevice));
}

Return<Status> ExtBiometricsFace::remove(uint32_t faceId) {
    ALOGD("remove faceId:%d", faceId);
    sp<AMessage> msg = new AMessage(REMOVE_REQUEST, mHandler);
    msg->setInt32("faceId", faceId);
    msg->post(0);
    return Status::OK;
    //return ErrorFilter(mDevice->remove(mDevice, faceId));
}

Return<Status> ExtBiometricsFace::authenticate(uint64_t operationId) {
    ALOGD("authenticate(operationId=%" PRId64 ")\n", operationId);
    std::lock_guard<std::mutex> lock(mCancelledMutex);
    mCancelled = false;
    sp<AMessage> msg = new AMessage(AUTH_REQUEST, mHandler);
    msg->setInt64("operationId", operationId);
    msg->post(0);
    return Status::OK;
    //return ErrorFilter(mDevice->authenticate(mDevice, operationId));
}

Return<Status> ExtBiometricsFace::userActivity() {
    ALOGD("userActivity");
    return ErrorFilter(mDevice->user_activity(mDevice));
}

Return<Status> ExtBiometricsFace::resetLockout(const hidl_vec<uint8_t>& hat) {
    ALOGD("resetLockout");
    const hw_auth_token_t* authToken =
        reinterpret_cast<const hw_auth_token_t*>(hat.data());
    return ErrorFilter(mDevice->reset_lockout(mDevice, authToken));
}

// Methods from ::vendor::sprd::hardware::face::V1_0::IExtBiometricsFace follow.
Return<Status> ExtBiometricsFace::doEnrollProcess(int64_t addr, const hidl_vec<int32_t>& info, const hidl_vec<int8_t>& byteInfo) {
    ALOGD("doEnrollProcess");
    sp<AMessage> msg = new AMessage(ENROLL_PROCESS_REQUEST, mHandler);
    size_t infoSize = info.size();
    void *pInfo = malloc(infoSize * sizeof(int32_t));
    memcpy(pInfo, (void*)info.data(), infoSize * sizeof(int32_t));
    size_t byteInfoSize = byteInfo.size();
    void *pByteInfo = malloc(byteInfoSize * sizeof(int8_t));
    memcpy(pByteInfo, (void*)byteInfo.data(), byteInfoSize * sizeof(int8_t));
    msg->setInt64("addr", addr);
    msg->setSize("infoSize", infoSize);
    msg->setPointer("pInfo", pInfo);
    msg->setSize("byteInfoSize", byteInfoSize);
    msg->setPointer("pByteInfo", pByteInfo);
    msg->post(0);
    return Status::OK;
}

Return<Status> ExtBiometricsFace::doAuthenticateProcess(int64_t main, int64_t sub, int64_t otp, const hidl_vec<int32_t>& info, const hidl_vec<int8_t>& byteInfo) {
    ALOGD("doAuthenticateProcess");
    sp<AMessage> msg = new AMessage(AUTH_PROCESS_REQUEST, mHandler);
    size_t infoSize = info.size();
    void *pInfo = malloc(infoSize * sizeof(int32_t));
    memcpy(pInfo, (void*)info.data(), infoSize * sizeof(int32_t));
    size_t byteInfoSize = byteInfo.size();
    void *pByteInfo = malloc(byteInfoSize * sizeof(int8_t));
    memcpy(pByteInfo, (void*)byteInfo.data(), byteInfoSize * sizeof(int8_t));
    msg->setInt64("main", main);
    msg->setInt64("sub", sub);
    msg->setInt64("otp", otp);
    msg->setSize("infoSize", infoSize);
    msg->setPointer("pInfo", pInfo);
    msg->setSize("byteInfoSize", byteInfoSize);
    msg->setPointer("pByteInfo", pByteInfo);
    msg->post(0);
    return Status::OK;
}

Return<Status> ExtBiometricsFace::updateLivenessMode(int32_t value, int32_t userId) {
    ALOGD("updateLivenessMode");
    char prop[128] = {0};
    char value_s[8] = {0};
    sprintf(prop, "persist.vendor.faceid.livenessmode%d", userId);
    sprintf(value_s, "%d", value);
    if(0 != property_set(prop, value_s)) {
        ALOGE("updateLivenessMode fail");
    }
    return Status::OK;
}

IExtBiometricsFace* ExtBiometricsFace::getInstance() {
    if (!sInstance) {
        sInstance = new ExtBiometricsFace();
    }
    return sInstance;
}

face_device_t* ExtBiometricsFace::openHal() {
    int err;
    const hw_module_t *hw_mdl = nullptr;
    ALOGD("Opening face hal library...");
    if (0 != (err = hw_get_module(FACE_HARDWARE_MODULE_ID, &hw_mdl))) {
        ALOGE("Can't open face HW Module, error: %d", err);
        return nullptr;
    }

    if (hw_mdl == nullptr) {
        ALOGE("No valid face module");
        return nullptr;
    }

    face_module_t const *module =
        reinterpret_cast<const face_module_t*>(hw_mdl);
    if (module->common.methods->open == nullptr) {
        ALOGE("No valid open method");
        return nullptr;
    }

    hw_device_t *device = nullptr;

    if (0 != (err = module->common.methods->open(hw_mdl, nullptr, &device))) {
        ALOGE("Can't open face methods, error: %d", err);
        return nullptr;
    }

    if (kVersion != device->version) {
        // enforce version on new devices because of HIDL@1.0 translation layer
        ALOGE("Wrong face version. Expected %d, got %d", kVersion, device->version);
        return nullptr;
    }

    face_device_t* face_device =
        reinterpret_cast<face_device_t*>(device);

    if (0 != (err =
            face_device->set_notify(face_device, ExtBiometricsFace::notify))) {
        ALOGE("Can't register face module callback, error: %d", err);
        return nullptr;
    }

    return face_device;
}

void ExtBiometricsFace::notify(const face_msg_t *msg) {
    ExtBiometricsFace* thisPtr = static_cast<ExtBiometricsFace*>(
            ExtBiometricsFace::getInstance());
    std::lock_guard<std::mutex> lock(thisPtr->mClientCallbackMutex);
    if (thisPtr == nullptr || thisPtr->mClientCallback == nullptr) {
        ALOGE("Receiving callbacks before the client callback is registered.");
        return;
    }
    const uint64_t devId = reinterpret_cast<uint64_t>(thisPtr->mDevice);
    switch (msg->type) {
        case FACE_ERROR: {
                ALOGD("onError(%d)", msg->data.error);
                if(FACE_ERROR_CANCELED != msg->data.error)
                {
                    std::lock_guard<std::mutex> lock(thisPtr->mCancelledMutex);
                    if(thisPtr->mCancelled) return; // if cancelled, just exit from cancel error
                }
                int32_t vendorCode = 0;
                FaceError result = VendorErrorFilter(msg->data.error, &vendorCode);
                sIsAlgoInitialized = false;
                if (!thisPtr->mClientCallback->onError(devId, thisPtr->mUserId, result, vendorCode).isOk()) {
                    ALOGE("failed to invoke faceId onError callback");
                }
            }
            break;
        case FACE_ACQUIRED: {
                ALOGD("onAcquired(%d)", msg->data.acquired);
                int32_t vendorCode = 0;
                FaceAcquiredInfo result = VendorAcquiredFilter(msg->data.acquired, &vendorCode);
                if (!thisPtr->mClientCallback->onAcquired(devId, thisPtr->mUserId, result, vendorCode).isOk()) {
                    ALOGE("failed to invoke faceId onAcquired callback");
                }
            }
            break;
        case FACE_TEMPLATE_REMOVED: {
                ALOGD("onRemoved(fid=%d)", msg->data.removed.fid);
                hidl_vec<uint32_t> removed(1); // unisoc support just 1 template
                uint32_t *list = removed.data();
                list[0] = msg->data.removed.fid;
                if (!thisPtr->mClientCallback->onRemoved(devId, removed, thisPtr->mUserId).isOk()) {
                    ALOGE("failed to invoke facdId onRemoved callback");
                }
            }
            break;
        case FACE_TEMPLATE_ENROLLING: {
                ALOGD("onEnrollResult(fid=%d)", msg->data.enroll.fid);
                {
                    std::lock_guard<std::mutex> lock(thisPtr->mCancelledMutex);
                    if(thisPtr->mCancelled) return; // if cancelled, just exit from cancel error
                }
                sIsAlgoInitialized = false;
                if(msg->data.enroll.fid <= 0) {
                    if (!thisPtr->mClientCallback->onError(devId, thisPtr->mUserId, FaceError::TIMEOUT, 0).isOk()) {
                        ALOGE("failed to invoke faceId onError callback");
                    }
                } else {
                    if (!thisPtr->mClientCallback->onEnrollResult(devId,
                            msg->data.enroll.fid, thisPtr->mUserId,0).isOk()) {
                        ALOGE("failed to invoke facdId onEnrollResult callback");
                    }
                }
            }
            break;
        case FACE_AUTHENTICATED: {
                ALOGD("onAuthenticated(fid=%d)", msg->data.authenticated.fid);
                {
                    std::lock_guard<std::mutex> lock(thisPtr->mCancelledMutex);
                    if(thisPtr->mCancelled) return; // if cancelled, just exit from cancel error
                }
                sIsAlgoInitialized = false;
                if (msg->data.authenticated.fid != 0) {
                    const uint8_t* hat =
                        reinterpret_cast<const uint8_t *>(&msg->data.authenticated.hat);
                    const hidl_vec<uint8_t> token(
                        std::vector<uint8_t>(hat, hat + sizeof(msg->data.authenticated.hat)));
                    if (!thisPtr->mClientCallback->onAuthenticated(devId,
                            msg->data.authenticated.fid, thisPtr->mUserId,
                            token).isOk()) {
                        ALOGE("failed to invoke faceId onAuthenticated callback");
                    }
                } else {
                    // Not a recognized face
                    if (!thisPtr->mClientCallback->onAuthenticated(devId,
                            msg->data.authenticated.fid, thisPtr->mUserId,
                            hidl_vec<uint8_t>()).isOk()) {
                        ALOGE("failed to invoke faceId onAuthenticated callback");
                    }
                }
            }
            break;
        case FACE_TEMPLATE_ENUMERATED: {
                ALOGD("onEnumerate(fid=%d)", msg->data.enumerated.fid);
                hidl_vec<uint32_t> enumerated(1); // unisoc support just 1 template
                uint32_t *list = enumerated.data();
                list[0] = msg->data.enumerated.fid;
                if (!thisPtr->mClientCallback->onEnumerate(devId, enumerated, thisPtr->mUserId).isOk()) {
                    ALOGE("failed to invoke facdId onEnumerate callback");
                }
            }
            break;
        case FACE_LOCKOUT_CHANGED: {
                uint32_t duration = (uint32_t)(msg->data.lockout.duration / 1000);
                ALOGD("onLockoutChanged(duration=%d)", duration);
                if (!thisPtr->mClientCallback->onLockoutChanged(msg->data.lockout.duration).isOk()) {
                    ALOGE("failed to invoke facdId onLockoutChanged callback");
                }
            }
            break;
        case FACE_ENROLL_PROCESSED:
            ALOGD("onEnrollProcessed(addr=%" PRId64", remaining=%d)",
                    msg->data.enroll_processed.addr,
                    msg->data.enroll_processed.remaining);
            if (!thisPtr->mExtClientCallback->onEnrollProcessed(devId,
                    msg->data.enroll_processed.addr).isOk()) {
                ALOGE("failed to invoke faceId onEnrollProcessed callback");
            }
            if (!thisPtr->mExtClientCallback->onEnrollResult(devId, 0,
                            thisPtr->mUserId,msg->data.enroll_processed.remaining).isOk()) {
                ALOGE("failed to invoke faceId onEnrollResult callback");
            }
            break;
        case FACE_AUTHENTICATE_PROCESSED:
            ALOGD("onAuthProcessed(main=%" PRId64", sub=%" PRId64")",
                    msg->data.authenticate_processed.main,
                    msg->data.authenticate_processed.sub);
            if (!thisPtr->mExtClientCallback->onAuthProcessed(devId,
                    msg->data.authenticate_processed.main,
                    msg->data.authenticate_processed.sub).isOk()) {
                ALOGE("failed to invoke faceId onAuthProcessed callback");
            }
            break;
        default:
            ALOGE("invalid msg type: %d", msg->type);
            return;
    }
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace face
}  // namespace hardware
}  // namespace sprd
}  // namespace vendor
