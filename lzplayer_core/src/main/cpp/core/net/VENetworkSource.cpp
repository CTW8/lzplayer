#include "VENetworkSource.h"

#include "VEHttpDataSource.h"
#include "utils/Log.h"
#include "VEDef.h"

namespace VE {
    namespace {
        /// AVIO 内部缓冲。太小会导致 FFmpeg 频繁回调，太大会拖长首次填充。
        constexpr int kAvioBufferSize = 64 * 1024;
    }

    VENetworkSource::VENetworkSource(std::shared_ptr<AMessage> &notify)
            : VEDemux(notify) {
    }

    VENetworkSource::~VENetworkSource() {
        if (mAvio) {
            // avio_context_free 不会释放我们给的缓冲，要自己来；
            // 而且它可能已经把 buffer 指针换掉了，以 mAvio->buffer 为准
            uint8_t *buf = mAvio->buffer;
            avio_context_free(&mAvio);
            av_freep(&buf);
            mAvioBuffer = nullptr;
        } else if (mAvioBuffer) {
            av_freep(&mAvioBuffer);
        }
        if (mDataSource) {
            mDataSource->close();
        }
    }

    int VENetworkSource::avioRead(void *opaque, uint8_t *buf, int size) {
        auto *self = static_cast<VENetworkSource *>(opaque);
        if (self == nullptr || self->mDataSource == nullptr) {
            return AVERROR_EOF;
        }
        const ssize_t got = self->mDataSource->readAt(self->mPosition, buf,
                                                      static_cast<size_t>(size));
        if (got > 0) {
            self->mPosition += got;
            return static_cast<int>(got);
        }
        if (got == 0) {
            return AVERROR_EOF;
        }
        // 读失败：可能是网络错误，也可能是 abort。两者都让 FFmpeg 尽快退出。
        return AVERROR(EIO);
    }

    int64_t VENetworkSource::avioSeek(void *opaque, int64_t offset, int whence) {
        auto *self = static_cast<VENetworkSource *>(opaque);
        if (self == nullptr || self->mDataSource == nullptr) {
            return AVERROR(EIO);
        }
        const int64_t total = self->mDataSource->size();

        // FFmpeg 用这个特殊 whence 问"总长多少"，不是真的定位
        if (whence == AVSEEK_SIZE) {
            return total > 0 ? total : AVERROR(ENOSYS);
        }

        int64_t target;
        switch (whence) {
            case SEEK_SET: target = offset; break;
            case SEEK_CUR: target = self->mPosition + offset; break;
            case SEEK_END:
                if (total <= 0) {
                    return AVERROR(ENOSYS);   // 长度未知，无法从尾部定位
                }
                target = total + offset;
                break;
            default:
                return AVERROR(EINVAL);
        }
        if (target < 0) {
            return AVERROR(EINVAL);
        }
        // 只记逻辑位置，真正的重定位推迟到下一次 read——
        // FFmpeg 探测容器时会来回 seek，提前重开连接是纯浪费
        self->mPosition = target;
        return target;
    }

    VEResult VENetworkSource::openInput(AVFormatContext *ctx, const std::string &path) {
        auto http = std::make_shared<VEHttpDataSource>();
        VEBufferedDataSource::Config config;   // 默认水位见头文件
        mDataSource = std::make_shared<VEBufferedDataSource>(http, config);

        // 缓冲事件经既有 notify 链路上抛，不另起通道
        mDataSource->setBufferingCallback(
                [this](int event, int percent) { onBufferingEvent(event, percent); });

        VEResult ret = mDataSource->open(path, 0);
        if (ret != VE_OK) {
            ALOGE("VENetworkSource::%s open failed: %d", __FUNCTION__, ret);
            mDataSource.reset();
            return ret;
        }
        mPosition = 0;

        mAvioBuffer = static_cast<uint8_t *>(av_malloc(kAvioBufferSize));
        if (mAvioBuffer == nullptr) {
            return VE_NO_MEMORY;
        }
        // seekable 取决于服务端是否支持 Range：readAt 失败会退化，
        // 这里统一按可 seek 声明，真定位不了时 avioSeek 会返回错误
        mAvio = avio_alloc_context(mAvioBuffer, kAvioBufferSize, 0, this,
                                   &VENetworkSource::avioRead, nullptr,
                                   &VENetworkSource::avioSeek);
        if (mAvio == nullptr) {
            av_freep(&mAvioBuffer);
            return VE_NO_MEMORY;
        }
        ctx->pb = mAvio;
        // 告诉 FFmpeg 这是自定义 IO，别去猜文件名对应的协议
        ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

        // 用空路径 open：数据全部来自 pb
        if (avformat_open_input(&mFormatContext, nullptr, nullptr, nullptr) != 0) {
            ALOGE("VENetworkSource::%s avformat_open_input failed", __FUNCTION__);
            return VE_UNKNOWN_ERROR;
        }
        ALOGI("VENetworkSource::%s opened %s (size=%lld)", __FUNCTION__,
              path.c_str(), static_cast<long long>(mDataSource->size()));
        return VE_OK;
    }

    void VENetworkSource::onBufferingEvent(int event, int percent) {
        // 从预取/消费线程被调用，只投消息：事件走 postNotify →
        // VEPlayer::onComponentEvent → Java，与其它组件事件同一条路
        postNotify(event, percent, 0, 0, nullptr);
    }

    void VENetworkSource::abort() {
        // 双通道解阻塞：FFmpeg 的 interrupt_callback 管解析层，
        // IDataSource::abort 关 socket 管 IO 层。少任何一个都可能让
        // release 在弱网下卡死。
        VEDemux::abort();
        if (mDataSource) {
            mDataSource->abort();
        }
    }
}
