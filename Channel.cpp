#include "Channel.h"
#include "Session.h"

namespace aidl::android::se {

Channel::Channel(std::weak_ptr<omapi::SecureElementSession> session,
            Terminal* terminal,
            int channelNumber,
            const std::vector<uint8_t>& selectResponse,
            const std::vector<uint8_t>& aid,
            const std::shared_ptr<ISecureElementListener>& listener)
    : mSession(std::move(session)),
      mTerminal(terminal),
      mChannelNumber(channelNumber),
      mSelectResponse(selectResponse),
      mAid(aid),
      mListener(listener) {}

int Channel::getChannelNumber() const {
    LOG(INFO) << "Channel number: " << mChannelNumber;
    return mChannelNumber;
}

void Channel::close() {
    LOG(INFO) << __func__;
    if (mIsClosed.exchange(true)) {
        return;
    }
    if (mTerminal) {
        mTerminal->closeChannel(this);
    }
    if (auto session = mSession.lock()) {
        session->removeChannel(this);
    }
}

std::vector<uint8_t> Channel::getSelectResponse() {
    return mSelectResponse;
}

std::vector<uint8_t> Channel::transmit(const std::vector<uint8_t>& command) {
    if (command.size() < 4) {
        LOG(ERROR) << "Channel " << mChannelNumber << ": APDU too short";
        return {};
    }

    std::vector<uint8_t> modifiedCommand = command;
    uint8_t originalCla = modifiedCommand[0];
    uint8_t newCla = originalCla;

    if (mChannelNumber > 0 && mChannelNumber <= 19) {
        if ((originalCla & 0x40) == 0) {
            if (mChannelNumber <= 3) {
                newCla = (originalCla & 0xFC) | static_cast<uint8_t>(mChannelNumber);
            } else {
                newCla = (originalCla & 0xF0) | 0x40 | static_cast<uint8_t>(mChannelNumber - 4);
            }
        } else {
            if (mChannelNumber <= 3) {
                 newCla = (originalCla & 0b11111100) | (uint8_t)mChannelNumber;
            } else {
                 newCla = (originalCla & 0b11110000) | 0b01000000 | (uint8_t)(mChannelNumber - 4);
            }
        }
    }
    if (mChannelNumber > 0) {
        modifiedCommand[0] = newCla;
    }

    std::vector<uint8_t> response = mTerminal->transmit(modifiedCommand);

    if (response.empty()) {
        LOG(ERROR) << "Channel " << mChannelNumber << ": Transmission failed (empty response from terminal).";
    }

    return response;
}

bool Channel::isBasicChannel() {
    LOG(INFO) << __PRETTY_FUNCTION__;
    LOG(INFO) << __func__ << ": " << (mChannelNumber == 0);
    return mChannelNumber == 0;
}

bool Channel::isClosed() {
    return mIsClosed.load();
}

uint8_t Channel::internalGetModifiedCla(uint8_t originalCla, int channelNumber) const {
    if (channelNumber < 0 || channelNumber > 19) { // Max logical channels often 19 (0-19 total)
        LOG(ERROR) << "Invalid channel number for CLA modification: " << channelNumber;
        return originalCla; // Return original if invalid
    }

    // Only modify for logical channels; basic channel (0) usually doesn't need client-side CLA mod.
    if (channelNumber == 0) {
        return originalCla;
    }

    uint8_t modifiedCla = originalCla;

    if ((originalCla & 0x80) == 0x00 || (originalCla & 0xC0) == 0x00) { // Standard class (e.g. 0x0X)
        if (channelNumber >= 1 && channelNumber <= 3) { // Logical channels 1-3
            modifiedCla = (originalCla & 0xBC); // Clear channel bits (b1,b0) and SM indication (b3,b2 if they were 01)
            modifiedCla |= static_cast<uint8_t>(channelNumber);
        } else if (channelNumber >= 4 && channelNumber <= 19) { // Logical channels 4-19
            modifiedCla = (originalCla & 0xB0); // Clear channel bits (b3-b0) and SM (keep b7,b5,b4)
            modifiedCla |= 0x40; // Indicate extended logical channel / set bit b6
            modifiedCla |= static_cast<uint8_t>(channelNumber - 4); // Channel ID in b3-b0
        }
    } else {
    
        if (channelNumber >= 1 && channelNumber <= 3) {
             modifiedCla = (originalCla & 0xBC) | (uint8_t)channelNumber; // Preserve b7, b5, b4
        } else if (channelNumber >=4 && channelNumber <= 19) {
             modifiedCla = (originalCla & 0xB0) | 0x40 | (uint8_t)(channelNumber - 4); // Preserve b7, b5
        }
    }

    return modifiedCla;
}

bool Channel::selectNext() {
    if (isClosed()) {
        LOG(ERROR) << "selectNext: Channel " << mChannelNumber << " is closed.";
        return false;
    }

    if (mAid.empty()) {
        LOG(ERROR) << "selectNext: No AID provided for channel " << mChannelNumber;
        return false;
    }

    if (!mTerminal) {
        LOG(ERROR) << "selectNext: Terminal is null for channel " << mChannelNumber;
        return false;
    }

    std::vector<uint8_t> selectCommand(5 + mAid.size());
    selectCommand[0] = 0x00;        // CLA byte (will be modified for channel)
    selectCommand[1] = 0xA4;        // INS_SELECT
    selectCommand[2] = 0x04;        // P1_SELECT_BY_DF_NAME
    selectCommand[3] = 0x02;        // P2_NEXT_OCCURRENCE
    selectCommand[4] = static_cast<uint8_t>(mAid.size()); // Lc
    std::copy(mAid.begin(), mAid.end(), selectCommand.begin() + 5);

    if (mChannelNumber != 0) {
        selectCommand[0] = internalGetModifiedCla(selectCommand[0], mChannelNumber);
    }

    std::vector<uint8_t> bufferSelectResponse = mTerminal->transmit(selectCommand);

    if (bufferSelectResponse.size() < 2) {
        LOG(ERROR) << "selectNext: Transmit failed or response too short (size: "
                   << bufferSelectResponse.size() << ")";
        return false;
    }

    uint8_t sw1 = bufferSelectResponse[bufferSelectResponse.size() - 2];
    uint8_t sw2 = bufferSelectResponse[bufferSelectResponse.size() - 1];
    uint16_t sw = (static_cast<uint16_t>(sw1) << 8) | sw2;

    if (((sw & 0xF000) == 0x9000) ||
        ((sw & 0xFF00) == 0x6200) ||
        ((sw & 0xFF00) == 0x6300)) {
        mSelectResponse = bufferSelectResponse;
        return true;
    }
    if ((sw & 0xFF00) == 0x6A00 && sw2 == 0x82) {
        return false;
    }
    LOG(ERROR) << "selectNext: Unsupported status word " << std::hex << sw
               << " on channel " << std::dec << mChannelNumber;
    return false;
}

SecureElementChannel::SecureElementChannel(const std::shared_ptr<Channel>& channel) {
    mChannel = std::shared_ptr<Channel>(channel);
    LOG(INFO) << __func__;
}

ndk::ScopedAStatus SecureElementChannel::close() {
    LOG(INFO) << __func__;
    mChannel->close();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementChannel::isClosed(bool* isClosed) {
    LOG(INFO) << __func__;
    *isClosed = mChannel->isClosed();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementChannel::isBasicChannel(bool* isBasicChannel) {
    LOG(INFO) << __func__;
    *isBasicChannel = mChannel->isBasicChannel();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementChannel::getSelectResponse(std::vector<uint8_t>* outSelectResponse) {
    LOG(INFO) << __func__;
    *outSelectResponse = mChannel->getSelectResponse();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementChannel::transmit(const std::vector<uint8_t>& command, std::vector<uint8_t>* outResponse) {
    LOG(INFO) << __func__;
    outResponse->clear();

    if (mChannel->isClosed()) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_ILLEGAL_STATE, "Channel is closed");
    }

    if (command.size() < 4) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_ILLEGAL_ARGUMENT, "APDU too short");
    }
    const uint8_t cla = command[0];
    const uint8_t ins = command[1];
    // Block MANAGE CHANNEL (OMAPI/SEAC).
    if (ins == 0x70) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_SECURITY, "MANAGE CHANNEL blocked");
    }
    // Block ISO-reserved CLA=0xFF with INS 0x6X/0x9X.
    if (cla == 0xFF && ((ins & 0xF0) == 0x60 || (ins & 0xF0) == 0x90)) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_SECURITY, "Reserved CLA/INS blocked");
    }
    // Block SELECT BY DF NAME on logical channels — applets must be (re)selected
    // via openLogicalChannel/selectNext, not raw transmit. Matches upstream
    // SecureElementChannel.transmit().
    if (!mChannel->isBasicChannel() && ins == 0xA4 && command.size() >= 3 && command[2] == 0x04) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_SECURITY, "SELECT by DF name blocked on logical channel");
    }

    std::vector<uint8_t> response = mChannel->transmit(command);
    if (response.empty()) {
        return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "HAL transmit failed");
    }
    *outResponse = std::move(response);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementChannel::selectNext(bool* isSelected) {
    LOG(INFO) << __func__;
    *isSelected = mChannel->selectNext();
    return ndk::ScopedAStatus::ok();
}

}
