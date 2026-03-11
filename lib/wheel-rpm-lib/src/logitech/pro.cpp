
#include "logitech_base.hpp"

namespace wheel {

class LOGI_PRO : public LogitechWheel {

public:
    using LogitechWheel::LogitechWheel;

    std::string name() const override
    {
        return "Logitech PRO";
    }
};

}
