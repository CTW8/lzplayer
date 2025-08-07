#ifndef LZPLAYER_VEBUNDLE_H
#define LZPLAYER_VEBUNDLE_H

#include <unordered_map>
#include <string>
#include <iostream>
#include <typeinfo>

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

            // 调试信息
#ifdef DEBUG_VEBUNDLE
            std::cout << "[VEBundle::set] Key: " << key 
                      << ", Type: " << typeid(typename std::decay<T>::type).name() << std::endl;
#endif

            return *this;
        }

        template<typename T>
        T get(const std::string &key, const T &default_value = T{}) const {
            auto it = args_.find(key);
            if (it == args_.end()) {
#ifdef DEBUG_VEBUNDLE
                std::cout << "[VEBundle::get] Key not found: " << key << std::endl;
#endif
                return default_value;
            }

            // 调试信息：显示存储的类型信息
#ifdef DEBUG_VEBUNDLE
            #if __cplusplus >= 201703L
            std::cout << "[VEBundle::get] Key: " << key 
                      << ", Stored type: " << it->second.type().name() 
                      << ", Requested type: " << typeid(T).name() << std::endl;
            #else
            std::cout << "[VEBundle::get] Key: " << key 
                      << ", Stored type: " << it->second.type().name() 
                      << ", Requested type: " << typeid(T).name() << std::endl;
            #endif
#endif

            try {
                // 尝试 any_cast
                if (auto *ptr = any_cast<T>(&it->second)) {
                    return *ptr;
                } else {
                    std::cerr << "[VEBundle] any_cast returned nullptr for key: " << key << std::endl;
                    std::cerr << "[VEBundle] Stored type: " << it->second.type().name()
                              << ", Requested type: " << typeid(T).name() << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "[VEBundle] any_cast exception for key: " << key
                          << ", what: " << e.what() << std::endl;
            }

            return default_value;
        }

        // 新增：类型安全的获取方法
        template<typename T>
        bool try_get(const std::string &key, T& out_value) const {
            auto it = args_.find(key);
            if (it == args_.end()) {
                return false;
            }

            if (auto *ptr = any_cast<T>(&it->second)) {
                out_value = *ptr;
                return true;
            }

            return false;
        }

        // 新增：获取存储的类型信息
        std::string get_type_name(const std::string &key) const {
            auto it = args_.find(key);
            if (it != args_.end()) {
                return it->second.type().name();
            }
            return "key_not_found";
        }

        // 新增：调试所有存储的键值对
        void debug_all() const {
            std::cout << "[VEBundle] All stored items:" << std::endl;
            for (const auto& pair : args_) {
                std::cout << "  Key: " << pair.first
                          << ", Type: " << pair.second.type().name() << std::endl;
            }
        }

        bool contains(const std::string &key) const {
            return args_.find(key) != args_.end();
        }

        // 新增：清除指定键
        void remove(const std::string &key) {
            args_.erase(key);
        }

        // 新增：清除所有
        void clear() {
            args_.clear();
        }

        // 新增：获取大小
        size_t size() const {
            return args_.size();
        }

    private:
        std::unordered_map<std::string, any_t> args_;
    };
}

#endif //LZPLAYER_VEBUNDLE_H