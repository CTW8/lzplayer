#ifndef __VE_PACKET_QUEUE__
#define __VE_PACKET_QUEUE__

#include <mutex>
#include <queue>
#include <memory>
#include "VEPacket.h"
namespace VE {
    class VEPacketQueue {
    public:
        explicit VEPacketQueue(int maxSize); // 显式构造函数
        ~VEPacketQueue() = default;

        // 删除拷贝构造函数和拷贝赋值操作符
        VEPacketQueue(const VEPacketQueue &) = delete;

        VEPacketQueue &operator=(const VEPacketQueue &) = delete;

        // 允许移动构造函数和移动赋值操作符
        VEPacketQueue(VEPacketQueue &&) noexcept = delete;

        VEPacketQueue &operator=(VEPacketQueue &&) noexcept = delete;

        // 放入队列，返回是否成功
        bool put(const std::shared_ptr<VEPacket> &pack);

        // 获取队列中的一个包，若队列为空则返回 nullptr
        std::shared_ptr<VEPacket> get();

        // 获取队列剩余空间
        int getRemainingSize() const;

        // 获取队列中数据的大小
        int getDataSize() const;

        // 清空队列
        void clear();

    private:
        std::queue<std::shared_ptr<VEPacket>> mPacketQueue;
        mutable std::mutex mMutex; // 可在 const 函数中锁定
        int mMaxSize; // 队列最大大小
    };
}

#endif // __VE_PACKET_QUEUE__