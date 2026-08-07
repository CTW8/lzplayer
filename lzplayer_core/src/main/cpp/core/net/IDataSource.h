#ifndef LZPLAYER_IDATASOURCE_H
#define LZPLAYER_IDATASOURCE_H

#include <cstdint>
#include <string>
#include <sys/types.h>
#include "VEError.h"

namespace VE {

    /// 字节流数据源：只管"按偏移取字节"，不认识任何容器格式。
    ///
    /// 这是「FFmpeg 只做 demux、网络 IO 自己实现」这条架构决策的落点：
    /// 本接口的实现负责取字节，再经自定义 AVIOContext 注入给 FFmpeg。
    class IDataSource {
    public:
        virtual ~IDataSource() = default;

        /// 打开并定位到 offset。阻塞，可被 abort 打断。
        virtual VEResult open(const std::string &url, int64_t offset) = 0;

        /// 从 offset 读最多 size 字节。返回实际读到的字节数；
        /// 0 表示流结束，负值表示错误。阻塞，可被 abort 打断。
        virtual ssize_t readAt(int64_t offset, void *buf, size_t size) = 0;

        /// 总长度；未知返回 -1(如 chunked 编码)
        virtual int64_t size() const = 0;

        /// 从任意线程调用：立刻中断阻塞中的 open/read。
        /// teardown 的有界性靠它——网络卡死时不能让 release 一直等下去。
        virtual void abort() = 0;

        virtual void close() = 0;
    };
}

#endif //LZPLAYER_IDATASOURCE_H
