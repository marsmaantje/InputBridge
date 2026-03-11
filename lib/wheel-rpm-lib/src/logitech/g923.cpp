
#include "logitech_base.hpp"

namespace wheel {

class LOGI_G923 : public LogitechWheel {

public:
    using LogitechWheel::LogitechWheel;

    std::string name() const override
    {
        return "Logitech G923";
    }
};

}
