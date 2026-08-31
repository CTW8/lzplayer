#ifndef LZPLAYER_VEHTTPDATASOURCE_H
#define LZPLAYER_VEHTTPDATASOURCE_H

#include <atomic>
#include <mutex>
#include <string>
#include "IDataSource.h"

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

namespace VE {

    /// HTTP/1.1 渐进式下载客户端（socket + OpenSSL）。
    ///
    /// 只做播放器需要的那部分：GET、Range 续传、重定向、超时。
    /// 明确不做：HTTP/2、代理、cookie、缓存语义。
    ///
    /// 顺序读是常态，`readAt` 的 offset 与当前流位置一致时直接continue读；
    /// 不一致就重开连接下新的 Range —— 上层(VEBufferedDataSource)会尽量
    /// 保证顺序，重定位只在真正的跨区 seek 时发生。
    class VEHttpDataSource : public IDataSource {
    public:
        VEHttpDataSource();
        ~VEHttpDataSource() override;

        VEResult open(const std::string &url, int64_t offset) override;
        ssize_t readAt(int64_t offset, void *buf, size_t size) override;
        int64_t size() const override { return mContentLength; }
        void abort() override;
        void close() override;

        /// abort 后要复用本对象须先复位(reset 之后重新 open)
        void clearAbort() { mAbort = false; }

    private:
        struct Url {
            std::string scheme, host, path;
            int port = 80;
            bool tls = false;
        };
        static bool parseUrl(const std::string &raw, Url *out);

        /// 建立 TCP(+TLS) 连接并发出 GET；解析响应头。
        /// 返回 VE_OK 时 mStreamPos == offset，可以开始读 body。
        VEResult connectAndRequest(const std::string &url, int64_t offset, int redirectsLeft);

        VEResult tcpConnect(const Url &url);
        VEResult tlsHandshake(const Url &url);
        void disconnect();

        /// 带超时与中断检查的原始读写
        ssize_t rawRead(void *buf, size_t size);
        ssize_t rawWrite(const void *buf, size_t size);
        /// 读一行(以 CRLF 结束)，用于解析响应头
        bool readLine(std::string *line);

        /// 顺序读回退：从当前流位置向前读并**丢弃**，直到 mStreamPos == target。
        ///
        /// 只在服务端忽略 Range(mNoRange)时使用。调用前 mStreamPos 必须已反映
        /// 连接的真实位置 —— connectAndRequest 里在 mConnected 置位之后调、
        /// readAt 里在确认 offset > mStreamPos 之后调。
        VEResult skipForwardTo(int64_t target);

        int mSocket = -1;
        SSL *mSsl = nullptr;
        SSL_CTX *mSslCtx = nullptr;

        std::string mUrl;
        /// 服务端告知的总长度；未知为 -1
        int64_t mContentLength = -1;
        /// 当前连接读到的绝对偏移
        int64_t mStreamPos = 0;
        bool mConnected = false;
        bool mEof = false;
        /// 服务端忽略了 Range(对 offset>0 的请求回 200 而不是 206)。
        /// **粘性**：忽略过一次就不会突然又支持，没必要每次重定位都再试一遍
        /// 并为此多付一个往返。open() 换 URL 时复位。
        bool mNoRange = false;

        /// 中断标志：abort() 从任意线程置位，阻塞读写每轮都检查
        std::atomic<bool> mAbort{false};
        /// 保护 socket/ssl 的销毁与使用(abort 会跨线程关 socket)
        std::mutex mConnMutex;

        /// readLine 的行缓冲(响应头解析用)
        std::string mLineBuf;
    };
}

#endif //LZPLAYER_VEHTTPDATASOURCE_H
