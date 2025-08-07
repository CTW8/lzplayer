#ifndef __VE_ANY__
#define __VE_ANY__

#include <typeinfo>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <string>
#include <vector>
#include <type_traits>

namespace VE {
// C++11 兼容的 make_unique 实现
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args &&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }

// 自定义异常类
    class bad_any_cast : public std::bad_cast {
    public:
        const char *what() const throw() {
            return "bad any_cast";
        }
    };

// Any 类的实现
    class Any {
    private:
        // 类型擦除的基类
        struct HolderBase {
            virtual ~HolderBase() {}
            virtual HolderBase *clone() const = 0;
            virtual const std::type_info &type() const = 0;
        };

        // 具体类型的持有者
        template<typename T>
        struct Holder : HolderBase {
            T value;

            explicit Holder(const T &val) : value(val) {}

            // C++11 兼容的移动构造（如果T支持移动）
            template<typename U>
            explicit Holder(U &&val, typename std::enable_if<
                    std::is_same<typename std::decay<U>::type, T>::value>::type * = 0)
                    : value(std::forward<U>(val)) {}

            HolderBase *clone() const {
                return new Holder<T>(value);
            }

            const std::type_info &type() const {
                return typeid(T);
            }
        };

        std::unique_ptr<HolderBase> holder_;

    public:
        // 默认构造函数
        Any() : holder_(nullptr) {}

        // 拷贝构造函数
        Any(const Any &other)
                : holder_(other.holder_ ?
                          std::unique_ptr<HolderBase>(other.holder_->clone()) : nullptr) {}

        // 移动构造函数
        Any(Any &&other)
                : holder_(std::move(other.holder_)) {}

        // 模板构造函数 - 接受任意类型
        template<typename T>
        Any(T &&value)
                : holder_(
                make_unique<Holder<typename std::decay<T>::type> >(std::forward<T>(value))) {}

        // 析构函数
        ~Any() {}

        // 拷贝赋值运算符
        Any &operator=(const Any &other) {
            if (this != &other) {
                holder_ = other.holder_ ?
                          std::unique_ptr<HolderBase>(other.holder_->clone()) : nullptr;
            }
            return *this;
        }

        // 移动赋值运算符
        Any &operator=(Any &&other) {
            if (this != &other) {
                holder_ = std::move(other.holder_);
            }
            return *this;
        }

        // 模板赋值运算符
        template<typename T>
        Any &operator=(T &&value) {
            holder_ = make_unique<Holder<typename std::decay<T>::type> >(std::forward<T>(value));
            return *this;
        }

        // 检查是否为空
        bool has_value() const {
            return holder_ != nullptr;
        }

        // 获取类型信息
        const std::type_info &type() const {
            return holder_ ? holder_->type() : typeid(void);
        }

        // 重置为空状态
        void reset() {
            holder_.reset();
        }

        // 交换两个 Any 对象
        void swap(Any &other) {
            holder_.swap(other.holder_);
        }

        // 调试辅助方法
        void debug_info() const {
            if (holder_) {
                std::cout << "Any contains type: " << holder_->type().name() << std::endl;
            } else {
                std::cout << "Any is empty" << std::endl;
            }
        }

        // 友元函数声明
        template<typename T>
        friend T any_cast(const Any &operand);

        template<typename T>
        friend T any_cast(Any &operand);

        template<typename T>
        friend T any_cast(Any &&operand);

        template<typename T>
        friend const T *any_cast(const Any *operand);

        template<typename T>
        friend T *any_cast(Any *operand);
    };

// 统一使用 dynamic_cast 的 any_cast 实现

// const Any& 版本
    template<typename T>
    T any_cast(const Any &operand) {
        using U = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

        auto* holder = dynamic_cast<const Any::Holder<U>*>(operand.holder_.get());
        if (holder) {
            return static_cast<T>(holder->value);
        }

        throw bad_any_cast();
    }

// Any& 版本
    template<typename T>
    T any_cast(Any &operand) {
        using U = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

        auto* holder = dynamic_cast<Any::Holder<U>*>(operand.holder_.get());
        if (holder) {
            return static_cast<T>(holder->value);
        }

        throw bad_any_cast();
    }

// Any&& 版本
    template<typename T>
    T any_cast(Any &&operand) {
        using U = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

        auto* holder = dynamic_cast<Any::Holder<U>*>(operand.holder_.get());
        if (holder) {
            return static_cast<T>(std::move(holder->value));
        }

        throw bad_any_cast();
    }

// const Any* 版本 - 修复为使用 dynamic_cast
    template<typename T>
    const T *any_cast(const Any *operand) {
        if (!operand || !operand->holder_) {
            return nullptr;
        }

        auto* holder = dynamic_cast<const Any::Holder<T>*>(operand->holder_.get());
        return holder ? &holder->value : nullptr;
    }

// Any* 版本 - 修复为使用 dynamic_cast
    template<typename T>
    T *any_cast(Any *operand) {
        if (!operand || !operand->holder_) {
            return nullptr;
        }

        auto* holder = dynamic_cast<Any::Holder<T>*>(operand->holder_.get());
        return holder ? &holder->value : nullptr;
    }

// 便利函数：make_any
    template<typename T, typename... Args>
    Any make_any(Args &&... args) {
        return Any(T(std::forward<Args>(args)...));
    }

// 工具函数：交换两个 Any 对象
    inline void swap(Any &lhs, Any &rhs) {
        lhs.swap(rhs);
    }
}

#endif