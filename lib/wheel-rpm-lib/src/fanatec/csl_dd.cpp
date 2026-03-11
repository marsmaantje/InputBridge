
#include "fanatec_base.hpp"

namespace wheel {

class FANATEC_CSL_DD : public FanatecWheel {

public:
    using FanatecWheel::FanatecWheel;

    std::string name() const override
    {
        return "Fanatec CSL_DD";
    }
};

}
