#pragma once

#include <atomic>
#include <vector>
#include <map>
#include <mutex>

#include <android-base/logging.h>
#include <android/binder_ibinder.h>

#include <utils/RefBase.h>

#include <aidl/android/hardware/secure_element/ISecureElement.h>
#include <aidl/android/hardware/secure_element/BnSecureElementCallback.h>

#include <aidl/android/se/omapi/ISecureElementListener.h>
#include <aidl/android/se/omapi/ISecureElementSession.h>
#include <aidl/android/se/omapi/ISecureElementReader.h>

#include <string>
#include <iostream>

using aidl::android::hardware::secure_element::ISecureElement;
using aidl::android::hardware::secure_element::BnSecureElementCallback;
using aidl::android::se::omapi::ISecureElementListener;
using aidl::android::se::omapi::ISecureElementSession;
using aidl::android::se::omapi::ISecureElementReader;

namespace aidl::android::se {
namespace omapi {
class SecureElementService;
};
class Channel;
class SecureElementReader;

using aidl::android::se::Channel;
using aidl::android::se::omapi::SecureElementService;

class Terminal : public ::android::RefBase {
public:
    Terminal(const std::string name);
    ~Terminal() override;

    void initialize(bool retryOnFail);
    void closeChannel(Channel* channel);
    void closeChannels();
    void close();
    std::string getName() const;
    std::vector<uint8_t> getAtr();
    std::shared_ptr<Channel> openBasicChannel(ISecureElementSession* session, const std::vector<uint8_t>& aid, uint8_t p2, const std::shared_ptr<ISecureElementListener>& listener);
    std::shared_ptr<Channel> openLogicalChannel(ISecureElementSession* session, const std::vector<uint8_t>& aid, uint8_t p2, const std::shared_ptr<ISecureElementListener>& listener);
    std::vector<uint8_t> transmit(const std::vector<uint8_t>& cmd);
    bool isSecureElementPresent();
    bool reset();
    std::shared_ptr<ISecureElementReader> newSecureElementReader(std::shared_ptr<SecureElementService> service);

private:
    void stateChange(bool state, const std::string& reason);
    void onClientDeath();
    static void onClientDeathWrapper(void* cookie);

    std::string mName;
    std::map<int, std::shared_ptr<Channel>> mChannels;
    std::mutex mLock;
    std::atomic<bool> mIsConnected{false};
    std::atomic<int> mGetHalRetryCount{0};
    std::shared_ptr<ISecureElement> mAidlHal;

    class AidlCallback : public BnSecureElementCallback {
        public:
            AidlCallback(Terminal* terminal);
            ::ndk::ScopedAStatus onStateChange(bool state, const std::string& debugReason) override;
            void clearTerminal();
        private:
            std::atomic<Terminal*> mTerminal;
        };

    bool mDefaultApplicationSelectedOnBasicChannel = true;

    const bool DEBUG = true;

    const int GET_SERVICE_DELAY_MILLIS = 4 * 1000;
    const int EVENT_GET_HAL = 1;
    const int EVENT_NOTIFY_STATE_CHANGE = 2;

    AIBinder_DeathRecipient* mDeathRecipient;
    std::shared_ptr<AidlCallback> mAidlCallback;

    void handler(int event, int msg, int delay);
    
    friend class SecureElementReader;
};
}  // namespace aidl::android::se