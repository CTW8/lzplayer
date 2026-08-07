#ifndef LZPLAYER_VESOURCEREGISTRY_H
#define LZPLAYER_VESOURCEREGISTRY_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "VESource.h"

namespace VE {

    /// 媒体源工厂注册表：scheme → 构造函数。
    ///
    /// 播放器只按路径的 scheme 查表建源，不认识任何具体实现。
    /// 新增协议(http/rtmp/自定义)只需注册一条，VEPlayer 零改动。
    class VESourceRegistry {
    public:
        using Factory =
                std::function<std::shared_ptr<VESource>(std::shared_ptr<AMessage> &notify)>;

        static VESourceRegistry &instance();

        /// scheme 用小写、不含 "://"，如 "file"/"http"/"https"
        void registerScheme(const std::string &scheme, Factory factory);

        /// 按 path 的 scheme 建源；无 scheme(本地绝对路径)按 "file" 处理。
        /// 找不到对应实现返回 nullptr。
        std::shared_ptr<VESource> create(const std::string &path,
                                         std::shared_ptr<AMessage> &notify) const;

        /// 从路径中取出小写 scheme；无 "://" 时返回 "file"
        static std::string schemeOf(const std::string &path);

    private:
        VESourceRegistry() = default;
        std::unordered_map<std::string, Factory> mFactories;
    };
}

#endif //LZPLAYER_VESOURCEREGISTRY_H
