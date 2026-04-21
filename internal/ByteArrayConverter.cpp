#include "ByteArrayConverter.h"

#include <iomanip>
#include <sstream>

std::string hex2string(const std::vector<uint8_t>& hex) {
    std::stringstream ss;
    for (size_t i = 0; i < hex.size(); ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(hex[i]);
        if (i + 1 < hex.size()) ss << ' ';
    }
    return ss.str();
}
