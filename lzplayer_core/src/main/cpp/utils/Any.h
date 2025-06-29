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

// any_cast 实现 - const Any& 版本
    template<typename T>
    T any_cast(const Any &operand) {
        typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type U;

        if (operand.holder_ && operand.holder_->type() == typeid(U)) {
            const Any::Holder<U> *holder = static_cast<const Any::Holder<U> *>(operand.holder_.get());
            return static_cast<T>(holder->value);
        }

        throw bad_any_cast();
    }

// any_cast 实现 - Any& 版本
    template<typename T>
    T any_cast(Any &operand) {
        typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type U;

        if (operand.holder_ && operand.holder_->type() == typeid(U)) {
            Any::Holder<U> *holder = static_cast<Any::Holder<U> *>(operand.holder_.get());
            return static_cast<T>(holder->value);
        }

        throw bad_any_cast();
    }

// any_cast 实现 - Any&& 版本
    template<typename T>
    T any_cast(Any &&operand) {
        typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type U;

        if (operand.holder_ && operand.holder_->type() == typeid(U)) {
            Any::Holder<U> *holder = static_cast<Any::Holder<U> *>(operand.holder_.get());
            return static_cast<T>(std::move(holder->value));
        }

        throw bad_any_cast();
    }

// any_cast 实现 - const Any* 版本
    template<typename T>
    const T *any_cast(const Any *operand) {
        if (operand && operand->holder_ && operand->holder_->type() == typeid(T)) {
            const Any::Holder<T> *holder = static_cast<const Any::Holder<T> *>(operand->holder_.get());
            return &holder->value;
        }
        return nullptr;
    }

// any_cast 实现 - Any* 版本
    template<typename T>
    T *any_cast(Any *operand) {
        if (operand && operand->holder_ && operand->holder_->type() == typeid(T)) {
            Any::Holder<T> *holder = static_cast<Any::Holder<T> *>(operand->holder_.get());
            return &holder->value;
        }
        return nullptr;
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

// 示例和测试代码
//int main() {
//    std::cout << "=== Custom Any Implementation Demo (C++11 Compatible) ===" << std::endl;
//
//    // 1. 基本使用示例
//    std::cout << "\n1. 基本使用：" << std::endl;
//
//    Any a1 = 42;
//    Any a2 = std::string("Hello, World!");
//    Any a3 = 3.14;
//
//    std::cout << "a1 contains: " << any_cast<int>(a1) << std::endl;
//    std::cout << "a2 contains: " << any_cast<std::string>(a2) << std::endl;
//    std::cout << "a3 contains: " << any_cast<double>(a3) << std::endl;
//
//    // 2. 类型检查
//    std::cout << "\n2. 类型检查：" << std::endl;
//    std::cout << "a1 type: " << a1.type().name() << std::endl;
//    std::cout << "a2 type: " << a2.type().name() << std::endl;
//    std::cout << "a3 type: " << a3.type().name() << std::endl;
//
//    // 3. 安全的指针转换
//    std::cout << "\n3. 安全的指针转换：" << std::endl;
//
//    int* ptr1 = any_cast<int>(&a1);
//    std::string* ptr2 = any_cast<std::string>(&a2);
//    float* ptr3 = any_cast<float>(&a3);  // 这会返回 nullptr
//
//    std::cout << "int* from a1: " << (ptr1 ? "valid" : "null") << std::endl;
//    std::cout << "string* from a2: " << (ptr2 ? "valid" : "null") << std::endl;
//    std::cout << "float* from a3: " << (ptr3 ? "valid" : "null") << std::endl;
//
//    // 4. 异常处理
//    std::cout << "\n4. 异常处理：" << std::endl;
//
//    try {
//        int wrong_cast = any_cast<int>(a2);  // 这会抛出异常
//    } catch (const bad_any_cast& e) {
//        std::cout << "捕获异常: " << e.what() << std::endl;
//    }
//
//    // 5. 复杂类型存储
//    std::cout << "\n5. 复杂类型存储：" << std::endl;
//
//    std::vector<int> vec;
//    vec.push_back(1);
//    vec.push_back(2);
//    vec.push_back(3);
//    vec.push_back(4);
//    vec.push_back(5);
//
//    Any a4 = vec;
//
//    std::vector<int> retrieved_vec = any_cast<std::vector<int> >(a4);
//    std::cout << "Vector contents: ";
//    for (std::vector<int>::iterator it = retrieved_vec.begin(); it != retrieved_vec.end(); ++it) {
//        std::cout << *it << " ";
//    }
//    std::cout << std::endl;
//
//    // 6. 拷贝和移动
//    std::cout << "\n6. 拷贝和移动：" << std::endl;
//
//    Any a5 = a1;  // 拷贝构造
//    Any a6 = std::move(a2);  // 移动构造
//
//    std::cout << "a5 (copy of a1): " << any_cast<int>(a5) << std::endl;
//    std::cout << "a6 (moved from a2): " << any_cast<std::string>(a6) << std::endl;
//    std::cout << "a2 after move has_value: " << (a2.has_value() ? "true" : "false") << std::endl;
//
//    // 7. 重置和检查
//    std::cout << "\n7. 重置和检查：" << std::endl;
//
//    std::cout << "a1 has_value: " << (a1.has_value() ? "true" : "false") << std::endl;
//    a1.reset();
//    std::cout << "a1 after reset has_value: " << (a1.has_value() ? "true" : "false") << std::endl;
//
//    // 8. make_any 使用
//    std::cout << "\n8. make_any 使用：" << std::endl;
//
//    Any a7 = make_any<std::string>("Created with make_any");
//    std::cout << "a7 contains: " << any_cast<std::string>(a7) << std::endl;
//
//    // 9. 自定义类型
//    std::cout << "\n9. 自定义类型：" << std::endl;
//
//    struct Person {
//        std::string name;
//        int age;
//
//        Person(const std::string& n, int a) : name(n), age(a) {}
//
//        void print() const {
//            std::cout << "Person: " << name << ", age: " << age << std::endl;
//        }
//    };
//
//    Any a8 = Person("Alice", 30);
//    Person person = any_cast<Person>(a8);
//    person.print();
//
//    // 10. 测试空 Any 对象
//    std::cout << "\n10. 空 Any 对象测试：" << std::endl;
//
//    Any empty_any;
//    std::cout << "Empty any has_value: " << (empty_any.has_value() ? "true" : "false") << std::endl;
//    std::cout << "Empty any type: " << empty_any.type().name() << std::endl;
//
//    try {
//        int invalid = any_cast<int>(empty_any);
//    } catch (const bad_any_cast& e) {
//        std::cout << "Empty any cast exception: " << e.what() << std::endl;
//    }
//
//    return 0;
//}