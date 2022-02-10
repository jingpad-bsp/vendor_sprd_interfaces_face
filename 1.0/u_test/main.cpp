#define LOG_TAG "IBiometricsFaceTest"

#include <log/log.h>
#include <android/log.h>

#include <android/hardware/biometrics/face/1.0/IBiometricsFace.h>
#include <android/hardware/biometrics/face/1.0/IBiometricsFaceClientCallback.h>
#include <hidl/HidlSupport.h>
#include <hidl/HidlTransportSupport.h>
#include <utils/Condition.h>

#include <vendor/sprd/hardware/face/1.0/IExtBiometricsFace.h>
#include <vendor/sprd/hardware/face/1.0/IExtBiometricsFaceClientCallback.h>

#include <cinttypes>
#include <cstdint>
#include <future>
#include <utility>

using android::Condition;
using android::Mutex;
using android::sp;
using android::hardware::hidl_vec;
using android::hardware::Return;
using android::hardware::biometrics::face::V1_0::FaceAcquiredInfo;
using android::hardware::biometrics::face::V1_0::FaceError;
using android::hardware::biometrics::face::V1_0::Feature;
using android::hardware::biometrics::face::V1_0::IBiometricsFace;
using android::hardware::biometrics::face::V1_0::IBiometricsFaceClientCallback;
using android::hardware::biometrics::face::V1_0::OptionalBool;
using android::hardware::biometrics::face::V1_0::OptionalUint64;
using android::hardware::biometrics::face::V1_0::Status;

using ::vendor::sprd::hardware::face::V1_0::IExtBiometricsFace;
using ::vendor::sprd::hardware::face::V1_0::IExtBiometricsFaceClientCallback;

typedef void (*test_case)(void);

static sp<IBiometricsFace> mService;
static sp<IExtBiometricsFace> mExtService;

const uint32_t kTimeout = 3;
const std::chrono::seconds kTimeoutInSeconds = std::chrono::seconds(kTimeout);
uint32_t kUserId = 99;
uint32_t kFaceId = 5;
const char kTmpDir[] = "/data/system/users/0/facedata";
const int kIterations = 1000;

#define ASSERTCALLBACKISSET [&](const OptionalUint64& res) { \
	if(Status::OK != res.status) { \
		ALOGE("Status::OK != res.status"); \
		cb_r = false; \
	} \
	if(0UL == res.value) { \
		ALOGE("0UL == res.value"); \
		cb_r = false; \
	} \
	promise.set_value(); \
}

// Wait for a callback to occur (signaled by the given future) up to the
// provided timeout. If the future is invalid or the callback does not come
// within the given time, returns false.
template <class ReturnType>
bool waitForCallback(std::future<ReturnType> future,
						std::chrono::milliseconds timeout = kTimeoutInSeconds) {
	auto expiration = std::chrono::system_clock::now() + timeout;
	if(!future.valid()) {
		ALOGE("future not valid");
	} else {
		std::future_status status = future.wait_until(expiration);
		if (status == std::future_status::ready) {
			return true;
		} else if(std::future_status::timeout == status) {
			ALOGE("Timed out waiting for callback");
		}
	}
	return false;
}

// Base callback implementation that just logs all callbacks by default
class FaceCallbackBase : public IBiometricsFaceClientCallback {
	public:
	Return<void> onEnrollResult(uint64_t, uint32_t, int32_t, uint32_t) override {
		ALOGD("Enroll callback called.");
		return Return<void>();
	}

	Return<void> onAuthenticated(uint64_t, uint32_t, int32_t, const hidl_vec<uint8_t>&) override {
		ALOGD("Authenticated callback called.");
		return Return<void>();
	}

	Return<void> onAcquired(uint64_t, int32_t, FaceAcquiredInfo, int32_t) override {
		ALOGD("Acquired callback called.");
		return Return<void>();
	}

	Return<void> onError(uint64_t, int32_t, FaceError, int32_t) override {
		ALOGD("Error callback called.");
		ALOGE("FaceCallbackBase onError");
		return Return<void>();
	}

	Return<void> onRemoved(uint64_t, const hidl_vec<uint32_t>&, int32_t) override {
		ALOGD("Removed callback called.");
		return Return<void>();
	}

	Return<void> onEnumerate(uint64_t, const hidl_vec<uint32_t>&, int32_t /* userId */) override {
		ALOGD("Enumerate callback called.");
		return Return<void>();
	}

	Return<void> onLockoutChanged(uint64_t) override {
		ALOGD("LockoutChanged callback called.");
		return Return<void>();
	}
};

class EnumerateCallback : public FaceCallbackBase {
	public:
	Return<void> onEnumerate(uint64_t, const hidl_vec<uint32_t>&, int32_t) override {
		promise.set_value();
		return Return<void>();
	}

	std::promise<void> promise;
};

class ErrorCallback : public FaceCallbackBase {
	public:
	ErrorCallback(bool filterErrors = false, FaceError errorType = FaceError::HW_UNAVAILABLE)
		: filterErrors(filterErrors), errorType(errorType), hasError(false) {}

	Return<void> onError(uint64_t, int32_t, FaceError error, int32_t) override {
		if ((filterErrors && errorType == error) || !filterErrors) {
			hasError = true;
			this->error = error;
			promise.set_value();
		}
		return Return<void>();
	}

	bool filterErrors;
	FaceError errorType;
	bool hasError;
	FaceError error;
	std::promise<void> promise;
};

class RemoveCallback : public FaceCallbackBase {
	public:
	explicit RemoveCallback(int32_t userId) : removeUserId(userId) {}

	Return<void> onRemoved(uint64_t, const hidl_vec<uint32_t>&, int32_t userId) override {
		if(removeUserId != userId) {
			ALOGE("removeUserId != userId");
		}
		promise.set_value();
		return Return<void>();
	}

	int32_t removeUserId;
	std::promise<void> promise;
};

class LockoutChangedCallback : public FaceCallbackBase {
	public:
	Return<void> onLockoutChanged(uint64_t duration) override {
		this->hasDuration = true;
		this->duration = duration;
		promise.set_value();
		return Return<void>();
	}
	bool hasDuration;
	uint64_t duration;
	std::promise<void> promise;
};

void ConnectTest() {
	ALOGD("ConnectTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<FaceCallbackBase> cb = new FaceCallbackBase();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(waitForCallback(promise.get_future()) && cb_r) {
		ALOGD("ConnectTest OK");
	} else {
		ALOGE("ConnectTest Fail");
	}
}

void ConnectNullTest() {
	ALOGD("ConnectNullTest");
	bool cb_r = true;
	std::promise<void> promise;
	mService->setCallback(nullptr, ASSERTCALLBACKISSET);
	if(waitForCallback(promise.get_future()) && cb_r) {
		ALOGD("ConnectNullTest OK");
	} else {
		ALOGE("ConnectNullTest Fail");
	}
}

void GenerateChallengeTest() {
	std::map<uint64_t, int> m;
	ALOGD("GenerateChallengeTest");
	for (int i = 0; i < kIterations; ++i) {
		bool challenge_r = true;
		std::promise<void> promise;
		mService->generateChallenge(kTimeout, [&](const OptionalUint64& res) {
			if(Status::OK != res.status) {
				ALOGE("Status::OK != res.status");
				challenge_r = false;
			}
			if(0UL == res.value) {
				ALOGE("0UL == res.value");
				challenge_r = false;
			}
			m[res.value]++;
			if(1UL != m[res.value]) {
				ALOGE("1UL != m[res.value]");
				challenge_r = false;
			}
			promise.set_value();
		});
		if(!waitForCallback(promise.get_future()) || !challenge_r) {
			ALOGE("GenerateChallengeTest Fail");
			return;
		}
	}
	ALOGD("GenerateChallengeTest OK");
}

void EnrollZeroHatTest() {
	ALOGD("EnrollZeroHatTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<ErrorCallback> cb = new ErrorCallback();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("EnrollZeroHatTest Fail");
		return;
	}

	hidl_vec<uint8_t> token(69);
	for (size_t i = 0; i < 69; i++) {
		token[i] = 0;
	}

	Return<Status> res = mService->enroll(token, kTimeout, {});
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		goto fail;
	}

	// At least one call to onError should occur
	if(!waitForCallback(cb->promise.get_future())) {
		ALOGE("waitForCallback return false");
		goto fail;
	}
	if(!cb->hasError) {
		ALOGE("cb has not error");
		goto fail;
	}
	ALOGD("EnrollZeroHatTest OK");
	return;

fail:
	ALOGE("EnrollZeroHatTest Fail");
}

void EnrollGarbageHatTest() {
	ALOGD("EnrollGarbageHatTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<ErrorCallback> cb = new ErrorCallback();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("EnrollGarbageHatTest Fail");
		return;
	}

	// Filling HAT with invalid data
	hidl_vec<uint8_t> token(69);
	for (size_t i = 0; i < 69; ++i) {
		token[i] = i;
	}

	Return<Status> res = mService->enroll(token, kTimeout, {});
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		goto fail;
	}

	// At least one call to onError should occur
	if(!waitForCallback(cb->promise.get_future())) {
		ALOGE("waitForCallback return false");
		goto fail;
	}
	if(!cb->hasError) {
		ALOGE("cb has not error");
		goto fail;
	}
	ALOGD("EnrollGarbageHatTest OK");
	return;

fail:
	ALOGE("EnrollGarbageHatTest Fail");
}

void SetFeatureZeroHatTest() {
	ALOGD("SetFeatureZeroHatTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<ErrorCallback> cb = new ErrorCallback();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("SetFeatureZeroHatTest Fail");
		return;
	}

	hidl_vec<uint8_t> token(69);
	for (size_t i = 0; i < 69; i++) {
		token[i] = 0;
	}

	Return<Status> res = mService->setFeature(Feature::REQUIRE_DIVERSITY, false, token, 0);
	if(Status::ILLEGAL_ARGUMENT != static_cast<Status>(res)) {
		ALOGE("Status::ILLEGAL_ARGUMENT != static_cast<Status>(res)");
		ALOGE("SetFeatureZeroHatTest Fail");
	} else {
		ALOGD("SetFeatureZeroHatTest OK");
	}
}

void SetFeatureGarbageHatTest() {
	ALOGD("SetFeatureGarbageHatTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<ErrorCallback> cb = new ErrorCallback();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("SetFeatureGarbageHatTest Fail");
		return;
	}

	// Filling HAT with invalid data
	hidl_vec<uint8_t> token(69);
	for (size_t i = 0; i < 69; ++i) {
		token[i] = i;
	}

	Return<Status> res = mService->setFeature(Feature::REQUIRE_DIVERSITY, false, token, 0);
	if(Status::ILLEGAL_ARGUMENT != static_cast<Status>(res)) {
		ALOGE("Status::ILLEGAL_ARGUMENT != static_cast<Status>(res)");
		ALOGE("SetFeatureGarbageHatTest Fail");
	} else {
		ALOGD("SetFeatureGarbageHatTest OK");
	}
}

bool CheckGetFeatureFails(sp<IBiometricsFace> service, int faceId, Feature feature) {
	std::promise<void> promise;
	bool gf_r = true;

	// Features cannot be retrieved for invalid faces.
	Return<void> res = service->getFeature(feature, faceId, [&](const OptionalBool& result) {
		if(Status::ILLEGAL_ARGUMENT != result.status) {
			ALOGE("Status::ILLEGAL_ARGUMENT != result.status");
			gf_r = false;
		}
		promise.set_value();
	});
	return waitForCallback(promise.get_future()) && gf_r;
}

void GetFeatureRequireAttentionTest() {
	ALOGD("GetFeatureRequireAttentionTest");
	if(CheckGetFeatureFails(mService, 0 /* faceId */, Feature::REQUIRE_ATTENTION)) {
		ALOGD("GetFeatureRequireAttentionTest OK");
	} else {
		ALOGE("GetFeatureRequireAttentionTest Fail");
	}
}

void GetFeatureRequireDiversityTest() {
	ALOGD("GetFeatureRequireDiversityTest");
	if(CheckGetFeatureFails(mService, 0 /* faceId */, Feature::REQUIRE_DIVERSITY)) {
		ALOGD("GetFeatureRequireDiversityTest OK");
	} else {
		ALOGE("GetFeatureRequireDiversityTest Fail");
	}
}

void RevokeChallengeTest() {
	ALOGD("RevokeChallengeTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<FaceCallbackBase> cb = new FaceCallbackBase();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("RevokeChallengeTest Fail");
		return;
	}

	auto start = std::chrono::system_clock::now();
	mService->revokeChallenge();
	auto elapsed = std::chrono::system_clock::now() - start;
	if(elapsed >= kTimeoutInSeconds) {
		ALOGE("elapsed >= kTimeoutInSeconds");
		ALOGE("RevokeChallengeTest Fail");
	} else {
		ALOGD("RevokeChallengeTest OK");
	}
}

void GetAuthenticatorIdTest() {
	ALOGD("GetAuthenticatorIdTest");
	bool ga_r = true;
	std::promise<void> promise;
	mService->getAuthenticatorId(
				[&](const OptionalUint64& res) {
				if(Status::OK != res.status) {
					ALOGE("Status::OK != res.status");
					ga_r = false;
				}
				promise.set_value();
			});
	if(!waitForCallback(promise.get_future()) || !ga_r) {
		ALOGE("GetAuthenticatorIdTest Fail");
	} else {
		ALOGD("GetAuthenticatorIdTest OK");
	}
}

void EnumerateTest() {
	ALOGD("EnumerateTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<EnumerateCallback> cb = new EnumerateCallback();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("EnumerateTest Fail");
		return;
	}

	Return<Status> res = mService->enumerate();
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		goto fail;
	}
	if(!waitForCallback(cb->promise.get_future())) {
		ALOGE("waitForCallback return false");
		goto fail;
	}
	ALOGD("EnumerateTest OK");
	return;

fail:
	ALOGE("EnumerateTest Fail");
}

void RemoveFaceTest() {
	ALOGD("RemoveFaceTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<ErrorCallback> cb = new ErrorCallback();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("RemoveFaceTest Fail");
		return;
	}

	// Remove a face
	Return<Status> res = mService->remove(kFaceId);
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		ALOGE("RemoveFaceTest Fail");
	} else {
		ALOGD("RemoveFaceTest OK");
	}
}

void RemoveAllFacesTest() {
	ALOGD("RemoveAllFacesTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<ErrorCallback> cb = new ErrorCallback();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("RemoveAllFacesTest Fail");
		return;
	}

	// Remove all faces
	Return<Status> res = mService->remove(0);
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		ALOGE("RemoveAllFacesTest Fail");
	} else {
		ALOGD("RemoveAllFacesTest OK");
	}
}

void SetActiveUserTest() {
	ALOGD("SetActiveUserTest");
	// Create an active user
	Return<Status> res = mService->setActiveUser(2, kTmpDir);
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		goto fail;
	}

	// Reset active user
	res = mService->setActiveUser(kUserId, kTmpDir);
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		goto fail;
	}
	ALOGD("SetActiveUserTest OK");
	return;

fail:
	ALOGE("SetActiveUserTest Fail");
}

void SetActiveUserUnwritableTest() {
	ALOGD("SetActiveUserUnwritableTest");
	// Create an active user to an unwritable location (device root dir)
	Return<Status> res = mService->setActiveUser(3, "/");
	if(Status::OK == static_cast<Status>(res)) {
		ALOGE("Status::OK == static_cast<Status>(res)");
		goto fail;
	}

	// Reset active user
	res = mService->setActiveUser(kUserId, kTmpDir);
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		goto fail;
	}
	ALOGD("SetActiveUserUnwritableTest OK");
	return;

fail:
	ALOGE("SetActiveUserUnwritableTest Fail");
}

void SetActiveUserNullTest() {
	ALOGD("SetActiveUserNullTest");
	// Create an active user to a null location.
	Return<Status> res = mService->setActiveUser(4, nullptr);
	if(Status::OK == static_cast<Status>(res)) {
		ALOGE("Status::OK == static_cast<Status>(res)");
		goto fail;
	}

	// Reset active user
	res = mService->setActiveUser(kUserId, kTmpDir);
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		goto fail;
	}
	ALOGD("SetActiveUserNullTest OK");
	return;

fail:
	ALOGE("SetActiveUserNullTest Fail");
}

void CancelTest() {
	ALOGD("CancelTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<ErrorCallback> cb = new ErrorCallback(true, FaceError::CANCELED);
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("CancelTest Fail");
		return;
	}

    Return<Status> res = mService->cancel();
	// check that we were able to make an IPC request successfully
	if(Status::OK != static_cast<Status>(res)) {
		ALOGE("Status::OK != static_cast<Status>(res)");
		goto fail;
	}

	// make sure callback was invoked within kTimeoutInSeconds
	if(!waitForCallback(cb->promise.get_future())) {
		ALOGE("waitForCallback return false");
		goto fail;
	}
	// check error should be CANCELED
	if(FaceError::CANCELED != cb->error) {
		ALOGE("FaceError::CANCELED != cb->error");
		goto fail;
	}
	ALOGD("CancelTest OK");
	return;

fail:
	ALOGE("CancelTest Fail");
}

void OnLockoutChangedTest() {
	ALOGD("OnLockoutChangedTest");
	bool cb_r = true;
	std::promise<void> promise;
	sp<LockoutChangedCallback> cb = new LockoutChangedCallback();
	mService->setCallback(cb, ASSERTCALLBACKISSET);
	if(!waitForCallback(promise.get_future()) || !cb_r) {
		ALOGE("OnLockoutChangedTest Fail");
		return;
	}

	// Update active user and ensure lockout duration 0 is received
    mService->setActiveUser(5, kTmpDir);

	// Make sure callback was invoked
	if(!waitForCallback(cb->promise.get_future())) {
		ALOGE("waitForCallback return false");
		goto fail;
	}

	// Check that duration 0 was received
	if(0 != cb->duration) {
		ALOGE("0 != cb->duration");
		goto fail;
	}
	ALOGD("OnLockoutChangedTest OK");
	return;

fail:
	ALOGE("OnLockoutChangedTest Fail");
}

static test_case s_cases[] = {
	ConnectTest,
	ConnectNullTest,
	GenerateChallengeTest,
	EnrollZeroHatTest,
	EnrollGarbageHatTest,
	SetFeatureZeroHatTest,
	SetFeatureGarbageHatTest,
	GetFeatureRequireAttentionTest,
	GetFeatureRequireDiversityTest,
	RevokeChallengeTest,
	GetAuthenticatorIdTest,
	EnumerateTest,
	RemoveFaceTest,
	RemoveAllFacesTest,
	SetActiveUserTest,
	SetActiveUserUnwritableTest,
	SetActiveUserNullTest,
	CancelTest,
	OnLockoutChangedTest,
};

int main(/*int argc, char** argv*/) {
	mService = IBiometricsFace::getService();
	mExtService = IExtBiometricsFace::getService();
	if(mExtService == nullptr) {
		ALOGE("IExtBiometricsFace::getService fail");
	} else {
		ALOGD("IExtBiometricsFace::getService successfully");
	}
	if(mService == nullptr) {
		ALOGE("IBiometricsFace::getService fail");
		return -1;
	} else {
		ALOGD("IBiometricsFace::getService successfully");
	}

	for(int i = 0; i < sizeof(s_cases)/sizeof(s_cases[0]); i++) {
		s_cases[i]();
	}

	return 0;
}
