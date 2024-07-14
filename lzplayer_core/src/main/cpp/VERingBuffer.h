//
// Created by 李振 on 2024/7/10.
//

#ifndef LZPLAYER_VERINGBUFFER_H
#define LZPLAYER_VERINGBUFFER_H

#include "Log.h"
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <algorithm>

class VERingBuffer {
private:
    uint8_t* buffer;
    size_t capacity;
    size_t readIndex;
    size_t writeIndex;
    size_t dataSize;

public:
    VERingBuffer(size_t size) : capacity(size), readIndex(0), writeIndex(0), dataSize(0) {
        buffer = static_cast<uint8_t*>(malloc(size));
        if (!buffer) {
            throw std::runtime_error("Failed to allocate memory for RingBuffer");
        }
    }

    ~VERingBuffer() {
        if (buffer) {
            free(buffer);
        }
    }

    size_t write(const uint8_t* data, size_t length) {
        size_t availableSpace = capacity - dataSize;
        size_t bytesToWrite = std::min(length, availableSpace);

        if (bytesToWrite == 0) {
            return 0; // Buffer is full, can't write
        }

        size_t firstWrite = std::min(bytesToWrite, capacity - writeIndex);
        memcpy(buffer + writeIndex, data, firstWrite);

        if (bytesToWrite > firstWrite) {
            memcpy(buffer, data + firstWrite, bytesToWrite - firstWrite);
        }

        writeIndex = (writeIndex + bytesToWrite) % capacity;
        dataSize += bytesToWrite;

        return bytesToWrite;
    }

    size_t read(uint8_t* data, size_t length) {
        size_t availableData = std::min(length, dataSize);

        size_t firstRead = std::min(availableData, capacity - readIndex);
        memcpy(data, buffer + readIndex, firstRead);

        if (availableData > firstRead) {
            memcpy(data + firstRead, buffer, availableData - firstRead);
        }

        readIndex = (readIndex + availableData) % capacity;
        dataSize -= availableData;

        return availableData;
    }

    size_t getAvailableData() const {
        return dataSize;
    }

    size_t getAvailableSpace() const {
        return capacity - dataSize;
    }
};

#endif //LZPLAYER_VERINGBUFFER_H
