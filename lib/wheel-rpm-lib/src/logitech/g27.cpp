
#include "logitech_base.hpp"

namespace wheel {

class LOGI_G27 : public LogitechWheel {

public:
    using LogitechWheel::LogitechWheel;

    std::string name() const override
    {
        return "Logitech G27";
    }
};

}
