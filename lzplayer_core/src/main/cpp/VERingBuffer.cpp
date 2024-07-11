//
// Created by 李振 on 2024/7/10.
//

#include "VERingBuffer.h"

VERingBuffer::VERingBuffer(int capacity) {
    mStart = (uint8_t*) malloc(capacity);
    memset(mStart,0,capacity);
    mEnd = mStart + capacity;
    mWriter = mRead = mStart;
    mMaxLen = capacity;
}

VERingBuffer::~VERingBuffer() {
    if(mStart){
        free(mStart);
        mStart = nullptr;
    }
}

void VERingBuffer::putData(uint8_t *data, int len) {
//    std::lock_guard<std::mutex> lk(mMutex);
    if(mWriter >= mRead){
        if(mEnd - mWriter >= len){
            memcpy(mWriter,data,len);
            mWriter += len;
        }else{
            if(mMaxLen - (mWriter - mRead) > len ){
                memcpy(mWriter,data,mEnd - mWriter);
                int remain = len - (mEnd - mWriter);
                memcpy(mStart,data + (mEnd - mWriter),remain);
                mWriter = mStart + remain;
            }
        }
    }else{
        if(mRead - mWriter >= len){
            memcpy(mWriter,data,len);
            mWriter += len;
        }
    }
}

int VERingBuffer::getFreeSize() {
    int ret =0;
    if(mWriter >= mRead){
        ret = mMaxLen - (mWriter-mRead);
    }else{
        ret = mRead - mWriter;
    }
    ALOGI("VERingBuffer::getFreeSize ret :%d",ret);
    return ret;
}

int VERingBuffer::getData(uint8_t *data, int len) {
//    std::lock_guard<std::mutex> lk(mMutex);

    if(mWriter >= mRead){
        if(mWriter - mRead >= len){
            memcpy(data,mRead,len);
            mRead += len;
        }else{
            ALOGI("there no enough buf");
            return -1;
        }
    }else{
        if(mMaxLen - (mRead - mWriter) > len){
            memcpy(data,mRead, mEnd - mRead);
            int remain = mMaxLen - (mEnd - mRead);
            memcpy(data + (mEnd-mRead),mStart,remain);
            mRead = mStart + remain;
        }else{
            ALOGI("there no enough buf");
            return -1;
        }
    }
    return 0;
}

int VERingBuffer::getRemainSize() {
    int ret =0;
    if(mWriter > mRead){
        ret = mWriter-mRead;
    }else{
        ret = mMaxLen - (mRead - mWriter);
    }
    ALOGI("VERingBuffer::getRemainSize ret:%d",ret);
    return ret;
}
