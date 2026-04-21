#include "Terminal.h"
#include "Reader.h"
#include "Session.h"
#include "Service.h"
#include "Channel.h"

#include "ByteArrayConverter.h"

#include <android/binder_manager.h>
#include <aidl/android/hardware/secure_element/LogicalChannelResponse.h>

#include <chrono>
#include <thread>

namespace aidl::android::se {
using aidl::android::se::omapi::SecureElementSession;

void Terminal::onClientDeath() {
    LOG(INFO) << __func__ << ": Die";
    mIsConnected = false;
    this->handler(EVENT_GET_HAL, 0, GET_SERVICE_DELAY_MILLIS);
}

void Terminal::onClientDeathWrapper(void* cookie) {
    LOG(INFO) << "Binder has died";
    Terminal* self = static_cast<Terminal*>(cookie);
    self->onClientDeath();
}

Terminal::AidlCallback::AidlCallback(Terminal* terminal) {
    mTerminal = terminal;
}

::ndk::ScopedAStatus Terminal::AidlCallback::onStateChange(bool state, const std::string& debugReason) {
    Terminal* t = mTerminal;
    if (t == nullptr) {
        LOG(WARNING) << __func__ << ": Terminal gone, ignoring state change";
        return ::ndk::ScopedAStatus::ok();
    }
    t->stateChange(state, debugReason);
    return ::ndk::ScopedAStatus::ok();
}

void Terminal::AidlCallback::clearTerminal() {
    mTerminal = nullptr;
}

Terminal::Terminal(const std::string name) {
    mName = name;
    mDeathRecipient = AIBinder_DeathRecipient_new(onClientDeathWrapper);
    mAidlCallback = ndk::SharedRefBase::make<AidlCallback>(this);
}

Terminal::~Terminal() {
    if (mAidlCallback != nullptr) {
        mAidlCallback->clearTerminal();
    }
    if (mAidlHal != nullptr) {
        AIBinder* binder = mAidlHal->asBinder().get();
        if (binder) {
            AIBinder_unlinkToDeath(binder, mDeathRecipient, this);
        }
    }
    if (mDeathRecipient != nullptr) {
        AIBinder_DeathRecipient_delete(mDeathRecipient);
        mDeathRecipient = nullptr;
    }
}

std::string Terminal::getName() const {
    return mName;
}

void Terminal::stateChange(bool state, const std::string& reason) {
    LOG(INFO) << __func__ << ": state: " << state << ", reason: " << reason;
    mIsConnected = state;
    if (!state) {
        LOG(INFO) << "state: not connected";
    } else {
        LOG(INFO) << "state: connected";
        this->closeChannels();
        mDefaultApplicationSelectedOnBasicChannel = true;
    }
    this->handler(EVENT_NOTIFY_STATE_CHANGE, state, 0);
}

std::vector<uint8_t> Terminal::transmit(const std::vector<uint8_t>& cmd) {
    LOG(INFO) << __func__;
    std::lock_guard<std::mutex> lock(mLock);

    if (!mIsConnected.load()) {
        return {};
    }
    if (mAidlHal == nullptr) {
        LOG(ERROR) << __func__ << ": mAidlHal is null";
        return {};
    }

    std::vector<uint8_t> curCmd = cmd;

    while (true) {
        std::vector<uint8_t> response;
        ndk::ScopedAStatus s = mAidlHal->transmit(curCmd, &response);
        if (!s.isOk()) {
            LOG(ERROR) << __func__ << ": HAL transmit failed: " << s.getDescription();
            return {};
        }
        if (response.size() < 2) {
            LOG(ERROR) << __func__ << ": response too short";
            return {};
        }

        uint8_t sw1 = response[response.size() - 2];
        uint8_t sw2 = response[response.size() - 1];

        // 0x6CXX: wrong Le, resend with Le = SW2.
        if (sw1 == 0x6C) {
            curCmd.back() = sw2;
            continue;
        }

        // 0x61XX: chained response; strip trailing SW, drain via GET RESPONSE.
        if (sw1 == 0x61) {
            response.resize(response.size() - 2);
            while (true) {
                std::vector<uint8_t> getResp = {cmd[0], 0xC0, 0x00, 0x00, sw2};
                std::vector<uint8_t> tmp;
                ndk::ScopedAStatus gs = mAidlHal->transmit(getResp, &tmp);
                if (!gs.isOk() || tmp.size() < 2) {
                    LOG(ERROR) << __func__ << ": GET RESPONSE failed";
                    return {};
                }
                uint8_t nsw1 = tmp[tmp.size() - 2];
                uint8_t nsw2 = tmp[tmp.size() - 1];
                response.insert(response.end(), tmp.begin(), tmp.end() - 2);
                if (nsw1 == 0x61) {
                    sw2 = nsw2;
                    continue;
                }
                response.push_back(nsw1);
                response.push_back(nsw2);
                return response;
            }
        }

        return response;
    }
}

void Terminal::initialize(bool retryOnFail) {
    LOG(INFO) << __func__;
    std::lock_guard<std::mutex> lock(mLock);
    if (mAidlHal == nullptr) {
        const std::string bName = std::string(ISecureElement::descriptor) + "/" + getName();
        LOG(INFO) << __func__ << ": Getting Secure Element service: " << bName;
        AIBinder* binder = AServiceManager_waitForService(bName.c_str());
        mAidlHal = ISecureElement::fromBinder(ndk::SpAIBinder(binder));
        if (mAidlHal != nullptr) {
            LOG(INFO) << __func__ << ": Successfully get SE service: " << bName;
            mAidlHal->init(mAidlCallback);
            AIBinder_linkToDeath(mAidlHal->asBinder().get(),
                                mDeathRecipient, this);
            mIsConnected = true;
        } else {
            LOG(ERROR) << __func__ << ": Failed to get SE service: " << bName;
        }
    }
}

std::shared_ptr<ISecureElementReader> Terminal::newSecureElementReader(std::shared_ptr<omapi::SecureElementService> service) {
    LOG(INFO) << __func__;
    return ndk::SharedRefBase::make<SecureElementReader>(service, ::android::sp<Terminal>(this));
}

std::shared_ptr<Channel> Terminal::openBasicChannel(ISecureElementSession* session, const std::vector<uint8_t>& aid, uint8_t p2, const std::shared_ptr<ISecureElementListener>& listener) {
    LOG(INFO) << __func__;
    if (!aid.empty() && (aid.size() < 5 || aid.size() > 16)) {
        LOG(ERROR) << __func__ << ": AID out of range";
        return nullptr;
    }
    if (!mIsConnected) {
        LOG(ERROR) << __func__ << ": SE is not connected";
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mLock);
    if (mAidlHal == nullptr) {
        LOG(ERROR) << __func__ << ": mAidlHal is null";
        return nullptr;
    }

    if (mChannels.count(0) > 0) {
        LOG(ERROR) << __func__ << ": basic channel already in use";
        return nullptr;
    }

    std::vector<uint8_t> selectResponse;
    ndk::ScopedAStatus oStatus =
        mAidlHal->openBasicChannel(aid.empty() ? std::vector<uint8_t>() : aid, p2, &selectResponse);
    if (!oStatus.isOk()) {
        LOG(ERROR) << __func__ << ": openBasicChannel HAL call failed: " << oStatus.getDescription();
        return nullptr;
    }

    LOG(INFO) << __func__ << ": basic channel opened, select response: " << hex2string(selectResponse);
    auto basicChannel = std::make_shared<Channel>(session, this, 0, selectResponse, aid, listener);
    mChannels.insert(std::make_pair(0, basicChannel));
    mDefaultApplicationSelectedOnBasicChannel = false;
    return basicChannel;
}

std::shared_ptr<Channel> Terminal::openLogicalChannel(ISecureElementSession* session, const std::vector<uint8_t>& aid, uint8_t p2, const std::shared_ptr<ISecureElementListener>& listener) {
    LOG(INFO) << __func__;
    if (!aid.empty() && (aid.size() < 5 || aid.size() > 16)) {
        LOG(ERROR) << __func__ << ": AID out of range";
        return nullptr;
    }
    if (!mIsConnected) {
        LOG(ERROR) << __func__ << ": SE is not connected";
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mLock);
    if (mAidlHal == nullptr) {
        LOG(ERROR) << __func__ << ": mAidlHal is null";
        return nullptr;
    }

    ::aidl::android::hardware::secure_element::LogicalChannelResponse aidlRs;
    ndk::ScopedAStatus oStatus = mAidlHal->openLogicalChannel(aid.empty() ? std::vector<uint8_t>() : aid, p2, &aidlRs);
    if (!oStatus.isOk()) {
        LOG(ERROR) << __func__ << ": openLogicalChannel failed: " << oStatus.getDescription();
        return nullptr;
    }

    int channelNumber = aidlRs.channelNumber;
    std::vector<uint8_t> selectResponse = aidlRs.selectResponse;
    LOG(INFO) << __func__ << ": channel " << channelNumber
              << ", select response: " << hex2string(selectResponse);

    auto logicalChannel = std::make_shared<Channel>(session, this, channelNumber, selectResponse, aid, listener);
    mChannels.insert(std::make_pair(channelNumber, logicalChannel));
    return logicalChannel;
}


bool Terminal::reset() {
    LOG(INFO) << __func__;
    return true;
}

void Terminal::closeChannel(Channel* channel) {
    LOG(INFO) << __func__;

    if (channel == nullptr) {
        LOG(WARNING) << __func__  << ": Attempt to close a null channel.";
        return;
    }

    if (mIsConnected) {
        if (mAidlHal != nullptr) {
            LOG(INFO) << __func__  << ": Closing channel " << channel->getChannelNumber() << " using AIDL HAL.";
            ndk::ScopedAStatus hal_status = mAidlHal->closeChannel(static_cast<int8_t>(channel->getChannelNumber()));
            if (!hal_status.isOk()) {
                if (!channel->isBasicChannel()) {
                    LOG(ERROR) << __func__  << ": Error closing non-basic AIDL channel " << channel->getChannelNumber()
                               << ". Status: " << hal_status.getDescription()
                               << ", ServiceSpecificError: " << hal_status.getServiceSpecificError();
                } else {
                    LOG(INFO) << __func__  << ": AIDL closeChannel for basic channel " << channel->getChannelNumber()
                              << " completed with status: " << hal_status.getDescription()
                              << " (ServiceSpecificError: " << hal_status.getServiceSpecificError() << "). This may be expected.";
                }
            }
        } else {
            LOG(WARNING) << __func__  << ": mAidlHal is null. Cannot close channel " << channel->getChannelNumber() << " via HAL.";
        }
    } else {
        LOG(WARNING) << __func__  << ": Not connected to SE. Channel " << channel->getChannelNumber() << " cannot be closed via HAL.";
    }

    int channelNumber = channel->getChannelNumber();
    auto it = mChannels.find(channelNumber);
    bool removedSuccessfully = false;

    if (it != mChannels.end()) {
        if (it->second.get() == channel) {
            mChannels.erase(it);
            removedSuccessfully = true;
            LOG(INFO) << __func__  << ": Channel " << channelNumber << " (instance " << channel << ") removed from map.";
        } else {
            LOG(WARNING) << __func__  << ": Channel " << channelNumber
                         << " found in map, but instance " << it->second.get()
                         << " does not match instance being closed " << channel
                         << ". Not removing from map.";
        }
    } else {
        LOG(WARNING) << __func__  << ": Channel " << channelNumber << " (instance " << channel << ") not found in map for removal (possibly already removed).";
    }
    if (mChannels.count(channelNumber) > 0) {
        LOG(ERROR) << __func__  << ": Channel number " << channelNumber << " still present in map after closeChannel operation. Current instance in map: " << mChannels.at(channelNumber).get();
    }
}

void Terminal::closeChannels() {
    LOG(INFO) << __func__;
    std::vector<std::shared_ptr<Channel>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mChannels.empty()) {
            return;
        }
        snapshot.reserve(mChannels.size());
        for (const auto& pair : mChannels) {
            snapshot.push_back(pair.second);
        }
    }

    // Release lock before close(): channel->close() re-enters Terminal::closeChannel,
    // which mutates mChannels; keeping the lock here would deadlock.
    for (const auto& channelPtr : snapshot) {
        if (channelPtr) {
            channelPtr->close();
        }
    }
}

void Terminal::close() {
    LOG(INFO) << __func__;
    if (mAidlHal != nullptr) {
        LOG(INFO) << __func__ << ": Unlinking death recipient.";
        AIBinder* binder = mAidlHal->asBinder().get();
        if (binder) {
            AIBinder_unlinkToDeath(binder, mDeathRecipient, this);
        }
    }
}

bool Terminal::isSecureElementPresent() {
    LOG(INFO) << __func__;
    bool p;
    if (mAidlHal != nullptr) {
        mAidlHal->isCardPresent(&p);
        LOG(INFO) << __func__ << ": " << p;
        return p;
    }
    LOG(ERROR) << __func__ << ": Can't find mAidlHal!, please init it first.";
    return false;
}

std::vector<uint8_t> Terminal::getAtr() {
    LOG(INFO) << __func__;
    std::vector<uint8_t> atr;

    if (!mIsConnected) {
        LOG(ERROR) << "Not connected";
        return atr;
    }

    if (mAidlHal != nullptr) {
        LOG(INFO) << "Fetching atr from AIDL hal";
        mAidlHal->getAtr(&atr);
        if (atr.empty()) {
            LOG(ERROR) << "Atr is empty!";
            return atr;
        }
    } else {
        LOG(ERROR) << "No AIDL hal found!";
        return atr;
    }
    if (DEBUG) {
        LOG(INFO) << "ATR: " << hex2string(atr);
    }
    return atr;
}

void Terminal::handler(int event, int /*msg*/, int delay) {
    if (event != EVENT_GET_HAL) {
        return;
    }
    constexpr int kMaxRetry = 5;
    if (mGetHalRetryCount.load() >= kMaxRetry) {
        LOG(ERROR) << __func__ << ": giving up HAL reconnect after "
                   << mGetHalRetryCount.load() << " attempts";
        return;
    }

    // Offload to a detached thread so binder death-recipient/state-change callbacks
    // are not blocked by waitForService and sleep_for.
    ::android::sp<Terminal> self(this);
    std::thread([self, delay]() {
        if (delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
        self->mGetHalRetryCount.fetch_add(1);
        self->initialize(self->mName.starts_with(SecureElementService::ESE_TERMINAL));
        if (self->mIsConnected.load()) {
            self->mGetHalRetryCount.store(0);
        }
    }).detach();
}

}
