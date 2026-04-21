#include "Service.h"
#include "Terminal.h"

namespace aidl::android::se::omapi {
    SecureElementService::SecureElementService() {
        createTerminals();
    }

    SecureElementService::~SecureElementService() = default;

    ndk::ScopedAStatus SecureElementService::getReaders(std::vector<std::string>* readers) {
        LOG(INFO) << __func__;
        std::lock_guard<std::mutex> lock(mTerminalsMutex);
        for (const auto& pair : mTerminals) {
            if (pair.first.find(ESE_TERMINAL) == 0) {
                readers->push_back(pair.first);
            }
        }
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus SecureElementService::getReader(const std::string& readerName,
                                                        std::shared_ptr<ISecureElementReader>* readerObj) {
        LOG(INFO) << __func__ << " for " << readerName;
        std::lock_guard<std::mutex> lock(mTerminalsMutex);
        auto it = mTerminals.find(readerName);
        if (it == mTerminals.end()) {
            LOG(ERROR) << __func__ << ": reader not found: " << readerName;
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                    EX_ILLEGAL_ARGUMENT, "Reader not found");
        }
        ::android::sp<Terminal> terminal = it->second;
        if (terminal == nullptr) {
            LOG(ERROR) << __func__ << ": terminal is null for " << readerName;
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                    EX_ILLEGAL_STATE, "Terminal is null");
        }
        *readerObj = terminal->newSecureElementReader(this->ref<SecureElementService>());
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus SecureElementService::isNfcEventAllowed(const std::string& /*readerName*/,
                                            const std::vector<uint8_t>& /*aid*/,
                                            const std::vector<std::string>& packageNames,
                                            int32_t /*userId*/,
                                            std::vector<bool>* isAllowed) {
        LOG(INFO) << __func__;
        // No NFC path in TWRP; deny all.
        isAllowed->assign(packageNames.size(), false);
        return ndk::ScopedAStatus::ok();
    }

    void SecureElementService::createTerminals() {
        const std::string name = std::string(ESE_TERMINAL) + "1";
        ::android::sp<Terminal> terminal = new Terminal(name);
        mTerminals.insert({name, terminal});
        // Match upstream Java SecureElementService.onCreate(): block in the
        // service-creation path until the SE HAL is connected. Otherwise the
        // first openSession() race after addService() returns
        // EX_ILLEGAL_STATE before the HAL has even bound.
        terminal->initialize(true);
    }
}