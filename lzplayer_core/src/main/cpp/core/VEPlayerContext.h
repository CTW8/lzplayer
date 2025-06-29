#ifndef __VE_PLAYER_CONTEXT__
#define __VE_PLAYER_CONTEXT__
#include <memory>
#include "AMessage.h"
namespace VE {
    class VEPlayerContext {

    public:
        VEPlayerContext(std::shared_ptr<AMessage> msg);

        ~VEPlayerContext();

    private:
        std::shared_ptr<AMessage> mPlayerNotify = nullptr;
    };
}
#endif