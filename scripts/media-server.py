#!/usr/bin/env python3
"""网络播放测试服务器（net-playback-harness 步骤2）。

**只绑 127.0.0.1**，经 `adb reverse tcp:8188 tcp:8188` 供给设备。
理由有二：设备侧确定性（不引入路由器变量），以及素材是用户私人文件，
绝不暴露到局域网。

本版是**最小可用版**：Range + 字节级请求日志，不含注入。
先探通再补注入 —— 播放器那条网络路径（VEHttpDataSource + VEBufferedDataSource
+ avio_alloc_context，约 1000 行手写代码）从未在设备上执行过一次，
若连 200/206 都走不通，带注入的完整服务器就是在为一条还不能用的通路做精细化。

用法:
  ./scripts/media-server.py [端口] [根目录]
  ./scripts/media-server.py 8188 assets/serving
"""
import http.server
import os
import socketserver
import sys
import threading
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8188
ROOT = sys.argv[2] if len(sys.argv) > 2 else "assets/serving"

LOG_PATH = os.path.join("assets", "server-requests.log")
_log_lock = threading.Lock()
_t0 = time.time()


def log(kind, detail):
    """字节级请求日志 —— 它是**独立交叉源**：服务端'每秒送出多少字节'
    对播放器'报了几次饥饿'，双源互证。没有它，starve 计数只能自己证明自己。"""
    line = "%.3f %s %s" % (time.time() - _t0, kind, detail)
    with _log_lock:
        with open(LOG_PATH, "a") as f:
            f.write(line + "\n")
    print(line, flush=True)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _safe_path(self):
        # 只允许根目录内的文件。私人素材目录必须防目录穿越
        rel = self.path.split("?", 1)[0].lstrip("/")
        rel = rel.replace("..", "")
        p = os.path.abspath(os.path.join(ROOT, rel))
        if not p.startswith(os.path.abspath(ROOT)) or not os.path.isfile(p):
            return None
        return p

    def do_HEAD(self):
        self._serve(head_only=True)

    def do_GET(self):
        self._serve(head_only=False)

    def _serve(self, head_only):
        p = self._safe_path()
        if p is None:
            log("404", self.path)
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        size = os.path.getsize(p)
        rng = self.headers.get("Range")
        start, end = 0, size - 1
        status = 200
        if rng and rng.startswith("bytes="):
            # Range 是 seek 的基础。**206 与 200 两条分支播放器都要能走**，
            # no-range 场景就是靠强制走 200 来验降级行为
            spec = rng[6:].split("-", 1)
            try:
                if spec[0]:
                    start = int(spec[0])
                if len(spec) > 1 and spec[1]:
                    end = int(spec[1])
            except ValueError:
                pass
            if start >= size:
                # 越界 Range 必须回 416 而不是钳进范围回 206。
                # 钳进去会返回一段"看起来正常"的数据, 掩盖掉
                # VEHttpDataSource 的状态码处理是否正确 —— 这正是
                # 本项目反复栽的那类"假成功"
                log("416", "%s range=%s size=%d" % (self.path, rng, size))
                self.send_response(416)
                self.send_header("Content-Range", "bytes */%d" % size)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            start = max(0, min(start, size - 1))
            end = max(start, min(end, size - 1))
            status = 206

        length = end - start + 1
        self.send_response(status)
        self.send_header("Content-Type", "video/mp4")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(length))
        if status == 206:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
        self.end_headers()
        log("REQ", "%s %s range=%s -> %d bytes=%d-%d/%d"
            % (self.command, self.path, rng or "-", status, start, end, size))
        if head_only:
            return

        sent = 0
        try:
            with open(p, "rb") as f:
                f.seek(start)
                while sent < length:
                    chunk = f.read(min(65536, length - sent))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    sent += len(chunk)
        except (BrokenPipeError, ConnectionResetError):
            # 播放器 seek 或 stop 会直接断连，这是正常的，不是错误
            log("ABORT", "%s sent=%d/%d" % (self.path, sent, length))
            return
        log("DONE", "%s sent=%d/%d" % (self.path, sent, length))

    def log_message(self, *a):
        pass  # 默认日志会刷屏，我们自己记


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    os.makedirs("assets", exist_ok=True)
    open(LOG_PATH, "w").close()
    print("serving %s on 127.0.0.1:%d" % (os.path.abspath(ROOT), PORT))
    print("设备侧: adb reverse tcp:%d tcp:%d" % (PORT, PORT))
    # 只绑回环，见文件头注释
    Server(("127.0.0.1", PORT), Handler).serve_forever()
