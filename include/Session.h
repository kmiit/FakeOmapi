#pragma once

#include <atomic>

#include <aidl/android/se/omapi/BnSecureElementSession.h>
#include <aidl/android/se/omapi/ISecureElementChannel.h>
#include <aidl/android/se/omapi/ISecureElementListener.h>
#include <aidl/android/se/omapi/ISecureElementReader.h>

using aidl::android::se::omapi::BnSecureElementSession;
using aidl::android::se::omapi::ISecureElementChannel;
using aidl::android::se::omapi::ISecureElementListener;
using aidl::android::se::omapi::ISecureElementReader;

namespace aidl::android::se {
class Channel;
class SecureElementChannel;
class SecureElementReader;
}

namespace aidl::android::se::omapi {
using aidl::android::se::SecureElementChannel;
using aidl::android::se::SecureElementReader;
using aidl::android::se::Channel;

class SecureElementSession : public BnSecureElementSession {
    public:
        SecureElementSession(std::shared_ptr<SecureElementReader> reader);
        virtual ~SecureElementSession();

        ::ndk::ScopedAStatus removeChannel(Channel* channel);

        ::ndk::ScopedAStatus getReader(std::shared_ptr<ISecureElementReader>* outReader);
        ::ndk::ScopedAStatus getAtr(std::vector<uint8_t>* outAtr);
        ::ndk::ScopedAStatus close();
        ::ndk::ScopedAStatus closeChannels();
        ::ndk::ScopedAStatus isClosed(bool* isClosed);
        ::ndk::ScopedAStatus openBasicChannel(const std::vector<uint8_t>& aid, int8_t p2,
                                                const std::shared_ptr<ISecureElementListener>& listener, std::shared_ptr<ISecureElementChannel>* outChannel);
        ::ndk::ScopedAStatus openLogicalChannel(const std::vector<uint8_t>& aid, int8_t p2,
                                                const std::shared_ptr<ISecureElementListener>& listener, std::shared_ptr<ISecureElementChannel>* outChannel);

    private:
        std::weak_ptr<SecureElementReader> mReader;
        std::vector<std::shared_ptr<Channel>> mChannels;
        std::mutex mLock;
        std::atomic<bool> mIsClosed{false};
        std::vector<uint8_t> mAtr;
};
}  // namespace aidl::android::se::omapi
