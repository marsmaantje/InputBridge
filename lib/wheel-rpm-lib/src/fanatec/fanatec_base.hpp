
#pragma once
#include "wheel/wheel.hpp"
#include "wheel/transport.hpp"
#include <memory>

namespace wheel {

class FanatecWheel : public Wheel {

protected:

    std::unique_ptr<HIDTransport> hid;

public:

    FanatecWheel(std::unique_ptr<HIDTransport> t)
        : hid(std::move(t)) {}

};

}
