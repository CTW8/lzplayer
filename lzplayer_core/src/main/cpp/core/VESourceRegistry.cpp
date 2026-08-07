#include "VESourceRegistry.h"

#include <algorithm>
#include <cctype>

#include "VEDemux.h"
#include "net/VENetworkSource.h"
#include "utils/Log.h"

namespace VE {

    VESourceRegistry &VESourceRegistry::instance() {
        static VESourceRegistry sInstance;
        static bool sBuiltinRegistered = [] {
            // 内建实现在首次取用时注册一次。本地文件是唯一内建源，
            // 网络源等由各自的模块在初始化时补注册。
            sInstance.registerScheme("file", [](std::shared_ptr<AMessage> &notify) {
                return std::static_pointer_cast<VESource>(std::make_shared<VEDemux>(notify));
            });
            const auto httpFactory = [](std::shared_ptr<AMessage> &notify) {
                return std::static_pointer_cast<VESource>(
                        std::make_shared<VENetworkSource>(notify));
            };
            sInstance.registerScheme("http", httpFactory);
            sInstance.registerScheme("https", httpFactory);
            return true;
        }();
        (void) sBuiltinRegistered;
        return sInstance;
    }

    void VESourceRegistry::registerScheme(const std::string &scheme, Factory factory) {
        mFactories[scheme] = std::move(factory);
    }

    std::string VESourceRegistry::schemeOf(const std::string &path) {
        const auto pos = path.find("://");
        if (pos == std::string::npos) {
            return "file";   // 本地绝对路径没有 scheme
        }
        std::string scheme = path.substr(0, pos);
        std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return scheme;
    }

    std::shared_ptr<VESource> VESourceRegistry::create(
            const std::string &path, std::shared_ptr<AMessage> &notify) const {
        const std::string scheme = schemeOf(path);
        auto it = mFactories.find(scheme);
        if (it == mFactories.end()) {
            ALOGE("VESourceRegistry::create no source registered for scheme '%s'",
                  scheme.c_str());
            return nullptr;
        }
        ALOGI("VESourceRegistry::create scheme '%s'", scheme.c_str());
        return it->second(notify);
    }
}
