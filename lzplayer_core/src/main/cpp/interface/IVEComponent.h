//
// Created by 李振 on 2025/6/29.
//

#ifndef LZPLAYER_IVECOMPONENT_H
#define LZPLAYER_IVECOMPONENT_H


#include "VEError.h"
#include "VEBundle.h"
namespace VE {
    class IVEComponent {
        virtual VEResult prepare(VEBundle params) = 0;

        /// start
        virtual VEResult start() = 0;

        /// stop
        virtual VEResult stop() = 0;

        ///seek
        virtual VEResult seekTo(uint64_t timestamp) = 0;

        ///flush
        virtual VEResult flush() = 0;

        /// pause
        virtual VEResult pause() = 0;

        /// release
        virtual VEResult release() = 0;
    };

}
#endif //LZPLAYER_IVECOMPONENT_H
