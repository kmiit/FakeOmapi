#pragma once

#include <atomic>
#include <memory>

#include <aidl/android/se/omapi/BnSecureElementChannel.h>
#include <aidl/android/se/omapi/ISecureElementListener.h>

#include "Terminal.h"

using aidl::android::se::omapi::BnSecureElementChannel;
using aidl::android::se::omapi::ISecureElementListener;

namespace aidl::android::se::omapi {
class SecureElementSession;
}

namespace aidl::android::se {

class Channel {
    public:
        Channel(std::weak_ptr<omapi::SecureElementSession> session,
            Terminal* terminal,
            int channelNumber,
            const std::vector<uint8_t>& selectResponse,
            const std::vector<uint8_t>& aid,
            const std::shared_ptr<ISecureElementListener>& listener);

        ~Channel() = default;

        void close();
        std::vector<uint8_t> transmit(const std::vector<uint8_t>& command);
        int getChannelNumber() const;
        std::vector<uint8_t> getSelectResponse();
        bool isBasicChannel();
        bool isClosed();
        bool selectNext();
    private:
        std::weak_ptr<omapi::SecureElementSession> mSession;
        Terminal* mTerminal;
        int mChannelNumber;
        std::vector<uint8_t> mSelectResponse;
        std::vector<uint8_t> mAid;
        const std::shared_ptr<ISecureElementListener> mListener;
        uint8_t internalGetModifiedCla(uint8_t originalCla, int channelNumber) const;
        std::atomic<bool> mIsClosed{false};
        friend class SecureElementChannel;
    };
    class SecureElementChannel : public BnSecureElementChannel {
        public:
            SecureElementChannel(const std::shared_ptr<Channel>& channel);
            ndk::ScopedAStatus close();
            ndk::ScopedAStatus isClosed(bool* isClosed);
            ndk::ScopedAStatus isBasicChannel(bool* _aidl_return);
            ndk::ScopedAStatus getSelectResponse(std::vector<uint8_t>* outSelectResponse);
            ndk::ScopedAStatus transmit(const std::vector<uint8_t>& command, std::vector<uint8_t>* outResponse);
            ndk::ScopedAStatus selectNext(bool* isSelected);
        private:
            std::shared_ptr<Channel> mChannel;
    };
}  // namespace aidl::android::se
