#ifndef __VE_FRAME_QUEUE__
#define __VE_FRAME_QUEUE__

#include "VEFrame.h"
#include <queue>
#include <mutex>
#include <memory>
namespace VE {
    class VEFrameQueue {
    public:
        explicit VEFrameQueue(int maxSize); // 显式构造函数
        ~VEFrameQueue() = default;

        // 删除拷贝构造函数和拷贝赋值操作符
        VEFrameQueue(const VEFrameQueue &) = delete;

        VEFrameQueue &operator=(const VEFrameQueue &) = delete;

        // 允许移动构造函数和移动赋值操作符
        VEFrameQueue(VEFrameQueue &&) noexcept = delete;

        VEFrameQueue &operator=(VEFrameQueue &&) noexcept = delete;

        // 放入队列，返回是否成功
        bool put(const std::shared_ptr<VEFrame> &frame);

        // 获取队列中的一个帧，若队列为空则返回 nullptr
        std::shared_ptr<VEFrame> get();

        // 获取队列剩余空间
        int getRemainingSize() const;

        // 获取队列中数据的数量
        int getDataSize() const;

        // 清空队列
        void clear();

    private:
        std::queue<std::shared_ptr<VEFrame>> mFrameQueue;
        mutable std::mutex mMutex; // 可在 const 函数中锁定
        int mMaxSize; // 队列最大大小
    };
}
#endif // __VE_FRAME_QUEUE__