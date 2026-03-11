
#include "fanatec_base.hpp"

namespace wheel {

class FANATEC_PODIUM_DD2 : public FanatecWheel {

public:
    using FanatecWheel::FanatecWheel;

    std::string name() const override
    {
        return "Fanatec PODIUM_DD2";
    }
};

}
