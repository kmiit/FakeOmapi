#include "Session.h"
#include "Channel.h"
#include "Reader.h"

#include "ByteArrayConverter.h"

namespace aidl::android::se::omapi {
using aidl::android::se::SecureElementReader;

SecureElementSession::SecureElementSession(std::shared_ptr<SecureElementReader> reader)
    : mReader(reader) {
    if (reader != nullptr) {
        mAtr = reader->getAtr();
    }
}

SecureElementSession::~SecureElementSession() = default;

::ndk::ScopedAStatus SecureElementSession::getReader(std::shared_ptr<ISecureElementReader>* outReader) {
    auto r = mReader.lock();
    if (r == nullptr) {
        *outReader = nullptr;
        return ::ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_ILLEGAL_STATE, "Reader is gone");
    }
    *outReader = std::static_pointer_cast<ISecureElementReader>(r);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SecureElementSession::getAtr(std::vector<uint8_t>* outAtr) {
    *outAtr = mAtr;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SecureElementSession::close() {
    LOG(INFO) << __func__;
    if (mIsClosed.exchange(true)) {
        return ::ndk::ScopedAStatus::ok();
    }
    closeChannels();
    if (auto r = mReader.lock()) {
        r->removeSession(this);
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SecureElementSession::removeChannel(Channel* channel) {
    LOG(INFO) << __func__;
    std::lock_guard<std::mutex> lock(mLock);
    mChannels.erase(
        std::remove_if(
            mChannels.begin(), mChannels.end(),
            [channel](const std::shared_ptr<Channel>& c) { return c.get() == channel; }
        ),
        mChannels.end()
    );
    LOG(INFO) << "Removed channel: " << channel->getChannelNumber();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SecureElementSession::closeChannels() {
    LOG(INFO) << __func__;
    std::vector<std::shared_ptr<Channel>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mLock);
        snapshot = mChannels;
    }
    for (auto& channel : snapshot) {
        if (channel) {
            channel->close();
            LOG(INFO) << "Closed channel: " << channel->getChannelNumber();
        }
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SecureElementSession::isClosed(bool* isClosed) {
    LOG(INFO) << __func__;
    *isClosed = mIsClosed.load();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SecureElementSession::openBasicChannel(const std::vector<uint8_t>& aid, int8_t p2,
    const std::shared_ptr<ISecureElementListener>& listener, std::shared_ptr<ISecureElementChannel>* outChannel) {
    LOG(INFO) << __func__ << " AID=" << hex2string(aid) << ", P2=" << int(p2);
    if (mIsClosed) {
        *outChannel = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(EX_SERVICE_SPECIFIC,
                                                                        "Session is closed");
    }
    if (listener == nullptr) {
        *outChannel = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(EX_SERVICE_SPECIFIC,
                                                                        "Listener is null");
    }
    if ((p2 != 0x00) && (p2 != 0x04) && (p2 != 0x08) && (p2 != 0x0C)) {
        LOG(ERROR) << __func__ << ": Unsupported p2: " << int(p2 & 0xFF);
        *outChannel = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(EX_SERVICE_SPECIFIC,
                                                                        "Unsupported p2 operation");
    }

    auto reader = mReader.lock();
    if (!reader) {
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(-1, "Reader is gone");
    }

    std::shared_ptr<Channel> channel =
        reader->getTerminal()->openBasicChannel(this, aid, p2, listener);
    if (channel == nullptr) {
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Failed to openBasicChannel");
    }

    std::lock_guard<std::mutex> lock(mLock);
    mChannels.push_back(channel);
    *outChannel = ndk::SharedRefBase::make<SecureElementChannel>(channel);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SecureElementSession::openLogicalChannel(const std::vector<uint8_t>& aid, int8_t p2,
    const std::shared_ptr<ISecureElementListener>& listener, std::shared_ptr<ISecureElementChannel>* outChannel) {
    LOG(INFO) << __func__ << " AID=" << hex2string(aid) << ", P2=" << int(p2);
    if (mIsClosed) {
        *outChannel = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(EX_SERVICE_SPECIFIC,
                                                                        "Session is closed");
    }
    if (listener == nullptr) {
        *outChannel = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(EX_SERVICE_SPECIFIC,
                                                                        "Listener is null");
    }
    if ((p2 != 0x00) && (p2 != 0x04) && (p2 != 0x08) && (p2 != 0x0C)) {
        LOG(ERROR) << __func__ << ": Unsupported p2: " << int(p2 & 0xFF);
        *outChannel = nullptr;
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(EX_SERVICE_SPECIFIC,
                                                                        "Unsupported p2 operation");
    }

    auto reader = mReader.lock();
    if (!reader) {
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(-1, "Reader is gone");
    }

    std::shared_ptr<Channel> channel =
        reader->getTerminal()->openLogicalChannel(this, aid, p2, listener);
    if (channel == nullptr) {
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Failed to openLogicalChannel");
    }

    std::lock_guard<std::mutex> lock(mLock);
    mChannels.push_back(channel);
    *outChannel = ndk::SharedRefBase::make<SecureElementChannel>(channel);
    return ::ndk::ScopedAStatus::ok();
}

}