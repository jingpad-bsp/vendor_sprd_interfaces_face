#define LOG_TAG "vendor.sprd.hardware.face@1.0-service"

#include <android/log.h>
#include <hidl/HidlSupport.h>
#include <hidl/HidlTransportSupport.h>
#include "ExtBiometricsFace.h"

using vendor::sprd::hardware::face::V1_0::IExtBiometricsFace;
using vendor::sprd::hardware::face::V1_0::implementation::ExtBiometricsFace;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::sp;

int main() {
    android::sp<IExtBiometricsFace> face = ExtBiometricsFace::getInstance();

    configureRpcThreadpool(1, true /*callerWillJoin*/);

    if (face != nullptr) {
        if(::android::OK != face->registerAsService()) {
            ALOGE("ExtBiometricsFace registerAsService fail");
            return 1;
        }
    } else {
        ALOGE("Can't create instance of BiometricsFace, nullptr");
    }

    joinRpcThreadpool();

    return 0; // should never get here
}
