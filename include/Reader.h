#pragma once

#include "Terminal.h"
#include "Service.h"

#include <aidl/android/se/omapi/BnSecureElementReader.h>
#include <aidl/android/se/omapi/ISecureElementSession.h>

using aidl::android::se::omapi::BnSecureElementReader;
using aidl::android::se::omapi::ISecureElementSession;

namespace aidl::android::se {
namespace omapi {
class SecureElementSession;
}

using aidl::android::se::omapi::SecureElementService;
using aidl::android::se::omapi::SecureElementSession;
using aidl::android::se::Terminal;

class SecureElementReader : public BnSecureElementReader {
    public:
        SecureElementReader(std::shared_ptr<SecureElementService> service,
                            ::android::sp<Terminal> terminal);
        ::ndk::ScopedAStatus isSecureElementPresent(bool* isTrue);
        ::ndk::ScopedAStatus openSession(std::shared_ptr<ISecureElementSession>* session);
        ::ndk::ScopedAStatus closeSessions();
        ::ndk::ScopedAStatus reset(bool* isReset);
        void removeSession(SecureElementSession* session);
        std::vector<uint8_t> getAtr();
        ::android::sp<Terminal> getTerminal();

    private:
        std::mutex mLock;
        std::shared_ptr<SecureElementService> mService;
        ::android::sp<Terminal> mTerminal;
        std::vector<std::shared_ptr<SecureElementSession>> mSessions;
};
}