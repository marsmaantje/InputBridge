// src/Devices/Wiimote/WiimoteManager.cpp
#include "WiimoteManager.h"
#include <SDL3/SDL_hidapi.h>
#include <cstring>

namespace InputBridge::Wiimote {

bool WiimoteManager::IsWiimoteProductString(const char *product) {
    if (!product) return false;
    // "Nintendo RVL-CNT-01" (Wiimote), "Nintendo RVL-CNT-01-TR" (Wiimote
    // Plus), "Nintendo RVL-WBC-01" (Balance Board) - see WiiBrew's SDP table.
    return std::strstr(product, "RVL-CNT-01") != nullptr ||
           std::strstr(product, "RVL-WBC-01") != nullptr;
}

std::vector<std::unique_ptr<WiimoteDevice>> WiimoteManager::Scan() {
    std::vector<std::unique_ptr<WiimoteDevice>> out;

    SDL_hid_device_info *devs = SDL_hid_enumerate(kVendorNintendo, 0);
    for (auto d = devs; d; d = d->next) {
        if (d->product_id != kProductWiimote && d->product_id != kProductWiimotePlus)
            continue;

        SDL_hid_device *hdev = SDL_hid_open_path(d->path);
        if (!hdev)
            continue; // already claimed (e.g. by SDL's own HIDAPI Wii driver), or a permissions issue

        // wchar_t* product_string from hidapi; convert defensively.
        bool is_balance_board = false;
        char product_utf8[64] = {};
        if (d->product_string) {
            size_t i = 0;
            for (; d->product_string[i] && i < sizeof(product_utf8) - 1; ++i)
                product_utf8[i] = char(d->product_string[i]);
            is_balance_board = IsWiimoteProductString(product_utf8) &&
                                std::strstr(product_utf8, "WBC") != nullptr;
        }

        auto device = std::make_unique<WiimoteDevice>(hdev, d->path ? d->path : "", is_balance_board);
        if (!device->Init()) {
            // Keep it anyway - Init() partially failing (e.g. one register
            // write dropped) shouldn't hide the device; Poll() will keep
            // trying to make sense of whatever reports do arrive.
        }
        out.push_back(std::move(device));
    }
    SDL_hid_free_enumeration(devs);

    return out;
}

} // namespace InputBridge::Wiimote
