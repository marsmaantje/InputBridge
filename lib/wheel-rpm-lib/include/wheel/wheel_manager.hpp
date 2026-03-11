
#pragma once
#include <memory>
#include <vector>
#include "wheel.hpp"
#include "device_id.hpp"

namespace wheel {

class WheelManager {
public:
    static std::vector<std::unique_ptr<Wheel>> scan();
};

}
