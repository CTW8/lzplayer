#include "VEHttpDataSource.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "utils/Log.h"
#include "VEDef.h"

namespace VE {
    namespace {
        constexpr int kConnectTimeoutMs = 5000;
        constexpr int kIoTimeoutMs = 10000;
        /// 单次 poll 的粒度：abort 最多在这个时间内生效
        constexpr int kPollSliceMs = 200;
        constexpr int kMaxRedirects = 5;

        void trim(std::string *s) {
            while (!s->empty() && (s->back() == '\r' || s->back() == '\n' ||
                                   s->back() == ' ' || s->back() == '\t')) {
                s->pop_back();
            }
            size_t start = 0;
            while (start < s->size() && ((*s)[start] == ' ' || (*s)[start] == '\t')) {
                ++start;
            }
            if (start > 0) {
                s->erase(0, start);
            }
        }

        std::string toLower(std::string s) {
            for (auto &c : s) {
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
            return s;
        }
    }

    VEHttpDataSource::VEHttpDataSource() = default;

    VEHttpDataSource::~VEHttpDataSource() {
        close();
    }

    bool VEHttpDataSource::parseUrl(const std::string &raw, Url *out) {
        const auto schemeEnd = raw.find("://");
        if (schemeEnd == std::string::npos) {
            return false;
        }
        out->scheme = toLower(raw.substr(0, schemeEnd));
        out->tls = (out->scheme == "https");
        if (!out->tls && out->scheme != "http") {
            return false;
        }
        out->port = out->tls ? 443 : 80;

        const size_t hostStart = schemeEnd + 3;
        size_t pathStart = raw.find('/', hostStart);
        std::string hostPort = (pathStart == std::string::npos)
                               ? raw.substr(hostStart)
                               : raw.substr(hostStart, pathStart - hostStart);
        out->path = (pathStart == std::string::npos) ? "/" : raw.substr(pathStart);

        // 忽略 userinfo（播放场景用不到，且带凭据的 URL 应该由上层处理）
        const auto at = hostPort.find('@');
        if (at != std::string::npos) {
            hostPort = hostPort.substr(at + 1);
        }
        // IPv6 字面量形如 [::1]:8080
        if (!hostPort.empty() && hostPort[0] == '[') {
            const auto close = hostPort.find(']');
            if (close == std::string::npos) {
                return false;
            }
            out->host = hostPort.substr(1, close - 1);
            if (close + 1 < hostPort.size() && hostPort[close + 1] == ':') {
                out->port = atoi(hostPort.c_str() + close + 2);
            }
        } else {
            const auto colon = hostPort.rfind(':');
            if (colon != std::string::npos) {
                out->host = hostPort.substr(0, colon);
                out->port = atoi(hostPort.c_str() + colon + 1);
            } else {
                out->host = hostPort;
            }
        }
        return !out->host.empty() && out->port > 0;
    }

    // ---------------------------------------------------------------------

    VEResult VEHttpDataSource::open(const std::string &url, int64_t offset) {
        close();
        mAbort = false;
        mUrl = url;
        mEof = false;
        return connectAndRequest(url, offset, kMaxRedirects);
    }

    VEResult VEHttpDataSource::tcpConnect(const Url &url) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", url.port);

        addrinfo *res = nullptr;
        const int gai = getaddrinfo(url.host.c_str(), portStr, &hints, &res);
        if (gai != 0 || res == nullptr) {
            ALOGE("VEHttpDataSource::%s getaddrinfo(%s) failed: %s",
                  __FUNCTION__, url.host.c_str(), gai_strerror(gai));
            return VE_PLAYER_ERROR_NETWORK_IO;
        }

        VEResult result = VE_PLAYER_ERROR_NETWORK_IO;
        for (addrinfo *ai = res; ai != nullptr; ai = ai->ai_next) {
            int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0) {
                continue;
            }
            // 非阻塞 connect + poll：这样 abort 才能在连接阶段也生效
            const int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
            if (rc < 0 && errno == EINPROGRESS) {
                int waited = 0;
                rc = -1;
                while (waited < kConnectTimeoutMs) {
                    if (mAbort) {
                        break;
                    }
                    pollfd pfd{fd, POLLOUT, 0};
                    const int pr = poll(&pfd, 1, kPollSliceMs);
                    waited += kPollSliceMs;
                    if (pr > 0) {
                        int err = 0;
                        socklen_t len = sizeof(err);
                        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                        rc = (err == 0) ? 0 : -1;
                        break;
                    }
                    if (pr < 0 && errno != EINTR) {
                        break;
                    }
                }
            }
            if (rc == 0) {
                const int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                std::lock_guard<std::mutex> lk(mConnMutex);
                mSocket = fd;
                result = VE_OK;
                break;
            }
            ::close(fd);
            if (mAbort) {
                result = VE_UNKNOWN_ERROR;
                break;
            }
        }
        freeaddrinfo(res);
        if (result != VE_OK) {
            ALOGE("VEHttpDataSource::%s connect %s:%d failed", __FUNCTION__,
                  url.host.c_str(), url.port);
        }
        return result;
    }

    VEResult VEHttpDataSource::tlsHandshake(const Url &url) {
        mSslCtx = SSL_CTX_new(TLS_client_method());
        if (mSslCtx == nullptr) {
            return VE_PLAYER_ERROR_NETWORK_IO;
        }
        // 系统 CA 路径；取不到时不强制校验(播放器场景优先可用性，
        // 需要严格校验的接入方应自行注入证书)
        SSL_CTX_set_default_verify_paths(mSslCtx);
        SSL_CTX_set_verify(mSslCtx, SSL_VERIFY_NONE, nullptr);

        mSsl = SSL_new(mSslCtx);
        if (mSsl == nullptr) {
            return VE_PLAYER_ERROR_NETWORK_IO;
        }
        SSL_set_fd(mSsl, mSocket);
        // SNI：多数 CDN 没有它会握手失败
        SSL_set_tlsext_host_name(mSsl, url.host.c_str());

        int waited = 0;
        while (waited < kIoTimeoutMs) {
            if (mAbort) {
                return VE_UNKNOWN_ERROR;
            }
            const int rc = SSL_connect(mSsl);
            if (rc == 1) {
                return VE_OK;
            }
            const int err = SSL_get_error(mSsl, rc);
            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                ALOGE("VEHttpDataSource::%s SSL_connect failed: %d", __FUNCTION__, err);
                return VE_PLAYER_ERROR_NETWORK_IO;
            }
            pollfd pfd{mSocket,
                       static_cast<short>(err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT), 0};
            poll(&pfd, 1, kPollSliceMs);
            waited += kPollSliceMs;
        }
        return VE_PLAYER_ERROR_NETWORK_TIMEOUT;
    }

    VEResult VEHttpDataSource::connectAndRequest(const std::string &url, int64_t offset,
                                                 int redirectsLeft) {
        if (redirectsLeft < 0) {
            ALOGE("VEHttpDataSource::%s too many redirects", __FUNCTION__);
            return VE_PLAYER_ERROR_NETWORK_IO;
        }
        Url parsed;
        if (!parseUrl(url, &parsed)) {
            ALOGE("VEHttpDataSource::%s bad url: %s", __FUNCTION__, url.c_str());
            return VE_INVALID_PARAMS;
        }

        VEResult ret = tcpConnect(parsed);
        if (ret != VE_OK) {
            return ret;
        }
        if (parsed.tls) {
            ret = tlsHandshake(parsed);
            if (ret != VE_OK) {
                disconnect();
                return ret;
            }
        }

        char req[2048];
        const int n = snprintf(req, sizeof(req),
                               "GET %s HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "User-Agent: LZPlayer/1.0\r\n"
                               "Accept: */*\r\n"
                               "Connection: keep-alive\r\n"
                               "Range: bytes=%lld-\r\n"
                               "\r\n",
                               parsed.path.c_str(), parsed.host.c_str(),
                               static_cast<long long>(offset));
        if (rawWrite(req, static_cast<size_t>(n)) != n) {
            disconnect();
            return VE_PLAYER_ERROR_NETWORK_IO;
        }

        // ---- 响应行 ----
        std::string line;
        if (!readLine(&line)) {
            disconnect();
            return VE_PLAYER_ERROR_NETWORK_IO;
        }
        int statusCode = 0;
        if (sscanf(line.c_str(), "HTTP/%*d.%*d %d", &statusCode) != 1) {
            ALOGE("VEHttpDataSource::%s bad status line: %s", __FUNCTION__, line.c_str());
            disconnect();
            return VE_PLAYER_ERROR_NETWORK_IO;
        }

        // ---- 头部 ----
        std::string location;
        int64_t contentLength = -1;
        int64_t rangeTotal = -1;
        bool chunked = false;
        while (readLine(&line)) {
            if (line.empty()) {
                break;   // 头部结束
            }
            const auto colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string key = toLower(line.substr(0, colon));
            std::string value = line.substr(colon + 1);
            trim(&key);
            trim(&value);

            if (key == "location") {
                location = value;
            } else if (key == "content-length") {
                contentLength = atoll(value.c_str());
            } else if (key == "transfer-encoding") {
                chunked = (toLower(value).find("chunked") != std::string::npos);
            } else if (key == "content-range") {
                // 形如 "bytes 100-999/1000"，斜杠后是资源总长
                const auto slash = value.rfind('/');
                if (slash != std::string::npos && value[slash + 1] != '*') {
                    rangeTotal = atoll(value.c_str() + slash + 1);
                }
            }
        }

        if (statusCode == 301 || statusCode == 302 || statusCode == 303 ||
            statusCode == 307 || statusCode == 308) {
            disconnect();
            if (location.empty()) {
                return VE_PLAYER_ERROR_NETWORK_IO;
            }
            ALOGI("VEHttpDataSource::%s redirect -> %s", __FUNCTION__, location.c_str());
            // 相对跳转补全成绝对 URL
            if (location.find("://") == std::string::npos) {
                const std::string base =
                        parsed.scheme + "://" + parsed.host + ":" + std::to_string(parsed.port);
                location = base + (location[0] == '/' ? location : "/" + location);
            }
            mUrl = location;
            return connectAndRequest(location, offset, redirectsLeft - 1);
        }

        // 206 = 部分内容(Range 生效)；200 = 服务端忽略了 Range，从头给
        if (statusCode != 200 && statusCode != 206) {
            ALOGE("VEHttpDataSource::%s HTTP %d", __FUNCTION__, statusCode);
            disconnect();
            return VE_PLAYER_ERROR_NETWORK_IO;
        }
        if (statusCode == 200 && offset > 0) {
            // 服务端不支持 Range：无法定位，上层只能从头读
            ALOGW("VEHttpDataSource::%s server ignored Range, got full body", __FUNCTION__);
            disconnect();
            return VE_INVALID_OPERATION;
        }
        if (chunked) {
            // 播放场景的容器基本都带 Content-Length；chunked 需要分块解析，
            // 不在本期范围
            ALOGE("VEHttpDataSource::%s chunked encoding not supported", __FUNCTION__);
            disconnect();
            return VE_INVALID_OPERATION;
        }

        mContentLength = (rangeTotal >= 0)
                         ? rangeTotal
                         : (contentLength >= 0 ? offset + contentLength : -1);
        mStreamPos = offset;
        mConnected = true;
        mEof = false;
        ALOGI("VEHttpDataSource::%s ready, status=%d offset=%lld total=%lld",
              __FUNCTION__, statusCode, static_cast<long long>(offset),
              static_cast<long long>(mContentLength));
        return VE_OK;
    }

    // ---------------------------------------------------------------------

    ssize_t VEHttpDataSource::readAt(int64_t offset, void *buf, size_t size) {
        if (mAbort) {
            return -1;
        }
        if (!mConnected || offset != mStreamPos) {
            // 不是顺序读：只能重开连接下新的 Range。
            // 上层缓冲层会尽量避免走到这里(见 VEBufferedDataSource)。
            ALOGI("VEHttpDataSource::%s reposition %lld -> %lld", __FUNCTION__,
                  static_cast<long long>(mStreamPos), static_cast<long long>(offset));
            disconnect();
            const VEResult ret = connectAndRequest(mUrl, offset, kMaxRedirects);
            if (ret != VE_OK) {
                return -1;
            }
        }
        if (mEof) {
            return 0;
        }
        const ssize_t got = rawRead(buf, size);
        if (got > 0) {
            mStreamPos += got;
        } else if (got == 0) {
            mEof = true;
        }
        return got;
    }

    ssize_t VEHttpDataSource::rawRead(void *buf, size_t size) {
        int waited = 0;
        while (waited < kIoTimeoutMs) {
            if (mAbort) {
                return -1;
            }
            ssize_t n;
            if (mSsl) {
                n = SSL_read(mSsl, buf, static_cast<int>(size));
                if (n > 0) {
                    return n;
                }
                const int err = SSL_get_error(mSsl, static_cast<int>(n));
                if (err == SSL_ERROR_ZERO_RETURN) {
                    return 0;
                }
                if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                    return -1;
                }
            } else {
                n = ::recv(mSocket, buf, size, 0);
                if (n >= 0) {
                    return n;
                }
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    return -1;
                }
            }
            pollfd pfd{mSocket, POLLIN, 0};
            poll(&pfd, 1, kPollSliceMs);
            waited += kPollSliceMs;
        }
        ALOGE("VEHttpDataSource::%s read timed out", __FUNCTION__);
        return -1;
    }

    ssize_t VEHttpDataSource::rawWrite(const void *buf, size_t size) {
        const uint8_t *p = static_cast<const uint8_t *>(buf);
        size_t sent = 0;
        int waited = 0;
        while (sent < size && waited < kIoTimeoutMs) {
            if (mAbort) {
                return -1;
            }
            ssize_t n;
            if (mSsl) {
                n = SSL_write(mSsl, p + sent, static_cast<int>(size - sent));
                if (n <= 0) {
                    const int err = SSL_get_error(mSsl, static_cast<int>(n));
                    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                        return -1;
                    }
                    n = 0;
                }
            } else {
                n = ::send(mSocket, p + sent, size - sent, MSG_NOSIGNAL);
                if (n < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        return -1;
                    }
                    n = 0;
                }
            }
            sent += static_cast<size_t>(n);
            if (sent < size) {
                pollfd pfd{mSocket, POLLOUT, 0};
                poll(&pfd, 1, kPollSliceMs);
                waited += kPollSliceMs;
            }
        }
        return static_cast<ssize_t>(sent);
    }

    bool VEHttpDataSource::readLine(std::string *line) {
        line->clear();
        char c;
        while (true) {
            const ssize_t n = rawRead(&c, 1);
            if (n <= 0) {
                return false;
            }
            if (c == '\n') {
                if (!line->empty() && line->back() == '\r') {
                    line->pop_back();
                }
                return true;
            }
            line->push_back(c);
            if (line->size() > 8192) {
                return false;   // 头部行异常长，视为坏响应
            }
        }
    }

    void VEHttpDataSource::abort() {
        mAbort = true;
        // 关掉 socket 让阻塞中的 poll/recv 立即返回——这是 teardown
        // 有界性的关键，不能只靠标志位等超时
        std::lock_guard<std::mutex> lk(mConnMutex);
        if (mSocket >= 0) {
            ::shutdown(mSocket, SHUT_RDWR);
        }
    }

    void VEHttpDataSource::disconnect() {
        std::lock_guard<std::mutex> lk(mConnMutex);
        if (mSsl) {
            SSL_free(mSsl);
            mSsl = nullptr;
        }
        if (mSslCtx) {
            SSL_CTX_free(mSslCtx);
            mSslCtx = nullptr;
        }
        if (mSocket >= 0) {
            ::close(mSocket);
            mSocket = -1;
        }
        mConnected = false;
    }

    void VEHttpDataSource::close() {
        disconnect();
        mContentLength = -1;
        mStreamPos = 0;
        mEof = false;
    }
}
