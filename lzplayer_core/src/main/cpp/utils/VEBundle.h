//
// Created by 李振 on 2025/6/29.
//

#ifndef LZPLAYER_VEBUNDLE_H
#define LZPLAYER_VEBUNDLE_H


#include <unordered_map>
#include <string>
#include <iostream>

#if __cplusplus >= 201703L
#include <any>
    using any_t = std::any;
    using std::any_cast;
#else

#include "Any.h"
    using any_t = VE::Any;
#endif

namespace VE {
    class VEBundle {
    public:
        template<typename T>
        VEBundle &set(const std::string &key, T &&value) {
            args_[key] = any_t(std::forward<T>(value));
            return *this;
        }

        template<typename T>
        T get(const std::string &key, const T &default_value = T{}) const {
            auto it = args_.find(key);
            if (it != args_.end()) {
                if (auto *ptr = any_cast<T>(&it->second)) {
                    return *ptr;
                } else {
                    std::cerr << "[VEBundle] Type mismatch: " << key << "\n";
                }
            }
            return default_value;
        }

        bool contains(const std::string &key) const {
            return args_.find(key) != args_.end();
        }

    private:
        std::unordered_map<std::string, any_t> args_;
    };
}
#endif //LZPLAYER_VEBUNDLE_H
