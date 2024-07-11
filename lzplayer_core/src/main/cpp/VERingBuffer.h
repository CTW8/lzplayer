//
// Created by 李振 on 2024/7/10.
//

#ifndef LZPLAYER_VERINGBUFFER_H
#define LZPLAYER_VERINGBUFFER_H

#include "Log.h"
#include <iostream>
class VERingBuffer {
public:
    VERingBuffer(int cap);
    ~VERingBuffer();

    void putData(uint8_t *data,int len);
    int getData(uint8_t *data,int len);

    int getFreeSize();
    int getRemainSize();

private:
    uint8_t *mStart = nullptr;
    uint8_t *mEnd = nullptr;
    uint8_t *mRead = nullptr;
    uint8_t *mWriter = nullptr;
    int32_t mMaxLen = 0;

    std::mutex mMutex;
};


#endif //LZPLAYER_VERINGBUFFER_H
