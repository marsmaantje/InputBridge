
#include "fanatec_base.hpp"

namespace wheel {

class FANATEC_CSW_V25 : public FanatecWheel {

public:
    using FanatecWheel::FanatecWheel;

    std::string name() const override
    {
        return "Fanatec CSW_V25";
    }
};

}
