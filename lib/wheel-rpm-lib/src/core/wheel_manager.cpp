
#include "wheel/wheel_manager.hpp"
#include <SDL3/SDL_hidapi.h>

namespace wheel {

std::vector<std::unique_ptr<Wheel>> WheelManager::scan()
{
    std::vector<std::unique_ptr<Wheel>> wheels;

    SDL_hid_device_info* devs = SDL_hid_enumerate(0,0);

    for(auto d = devs; d; d = d->next)
    {
        // VID/PID detection placeholder
        // Example mapping:
        // Logitech G29: 046D:C24F
        // Logitech G923: 046D:C266
        // Fanatec CSL DD: 0EB7:0005
        // Moza R9: 346E:0009

        (void)d;
    }

    SDL_hid_free_enumeration(devs);

    return wheels;
}

}
