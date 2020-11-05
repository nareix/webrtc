#ifndef __UTIL_HPP__
#define __UTIL_HPP__

#include <memory>
#include <algorithm>

#include <chrono>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <random>

#include "rtc_base/json.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

//
// singleton
//

template <class T>
class Singular
{
public:
        template <class... Args>
        static T* Instance(Args... _args) {
                static auto getInstance = [_args...]() -> T& {
                        return GetInstance(_args...);
                };
                return GetOne(getInstance);
        }
protected:
        Singular() = delete;
        ~Singular() = delete;
        Singular(Singular&) = delete;
        Singular& operator=(const Singular&) = delete;
private:
        static T* GetOne(const std::function<T&()>& _getInstance) {
                static T& instance = _getInstance();
                return &instance;
        }
        template<class... Args>
        static T& GetInstance(Args... _args) {
                static T instance{std::forward<Args>(_args)...};
                return instance;
        }
};

//
// defer
//

typedef struct _Defer
{
        _Defer(const std::function<void()>& _deferredFunc): m_deferredFunc(_deferredFunc) {}
        ~_Defer() { m_deferredFunc(); }
private:
        const std::function<void()> m_deferredFunc;
        _Defer() = delete;
        _Defer(const _Defer&) = delete;
        _Defer(_Defer&&) = delete;
        _Defer &operator=(const _Defer&) = delete;
        _Defer &operator=(_Defer&&) = delete;
        void *operator new(std::size_t, ...) = delete;
        void operator delete(void*) = delete;
        void operator delete[](void*) = delete;
} Defer;

//
// queue
//

template <class T>
class SharedQueue
{
public:
        SharedQueue(size_t nItems);
        SharedQueue();
        T Pop();
        bool TryPop(T &item);
        bool PopWithTimeout(T& item, const std::chrono::milliseconds& timeout);
        void Push(const T& item);
        void Push(T&& item);
        bool Peek(T& item);
        bool TryPush(const T& item);
        bool ForcePush(const T& item);
        bool ForcePush(const T&& item);
        void Clear();

        // though I do not recommend using empty() or size() in concurrent environment
        // but it is natural to use it for debugging or verbose outputs purpose
        bool IsEmpty();
        size_t Size();

        // access to iterators
        size_t Foreach(const std::function<void(T&)> lambda);
        void FindIf(const std::function<bool(T&)> _lambda);
        void CriticalSection(const std::function<void(std::deque<T>&)> lambda);
private:
        std::deque<T> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_consumerCondition;
        std::condition_variable m_producerCondition;
        size_t m_nMaxItems;
};

template <class T>
SharedQueue<T>::SharedQueue() :
        m_queue(std::deque<T>()),
        m_mutex(),
        m_consumerCondition(),
        m_producerCondition(),
        m_nMaxItems(0)
{}

template <class T>
SharedQueue<T>::SharedQueue(size_t _nItems):
        m_queue(std::deque<T>()),
        m_mutex(),
        m_consumerCondition(),
        m_producerCondition(),
        m_nMaxItems(_nItems)
{}

template <class T>
bool SharedQueue<T>::Peek(T& _item)
{      
        std::unique_lock<std::mutex> mutexLock(m_mutex);
        if (m_queue.empty()) {
                return false;
        }
        if (!m_queue.empty()) {
                _item = m_queue.back();
                return true;
        }
        return false;
}

template <class T>
bool SharedQueue<T>::PopWithTimeout(T& _item, const std::chrono::milliseconds& _timeout)
{
        std::unique_lock<std::mutex> mutexLock(m_mutex);
        if (m_queue.empty()) {
                auto ret = m_consumerCondition.wait_for(mutexLock, _timeout);
                if (ret == std::cv_status::timeout) {
                        return false; 
                }
        }
        if (!m_queue.empty()) {
                _item = m_queue.back();
                m_queue.pop_back();
                m_producerCondition.notify_one();
                return true;
        }
        return false;
}

template <class T>
T SharedQueue<T>::Pop()
{
        std::unique_lock<std::mutex> mutexLock(m_mutex);
        while (m_queue.empty()) {
                m_consumerCondition.wait(mutexLock);
        }
        auto item = m_queue.back();
        m_queue.pop_back();
        m_producerCondition.notify_one();
        return item;
}

template <class T>
bool SharedQueue<T>::TryPop(T& _item)
{
        std::unique_lock<std::mutex> mutexLock(m_mutex);
        if (! m_queue.empty()) {
                _item = m_queue.back();
                m_queue.pop_back();
                m_producerCondition.notify_one();
                return true;
        }
        return false;
}

template <class T>
void SharedQueue<T>::Push(const T& _item)
{
        std::unique_lock<std::mutex> mutexLock(m_mutex);
        if (m_nMaxItems != 0 && m_queue.size() >= m_nMaxItems) {
                m_producerCondition.wait(mutexLock);
        }
        m_queue.push_front(_item);
        m_consumerCondition.notify_one();
}

template <class T>
bool SharedQueue<T>::ForcePush(const T& _item)
{
        bool bRet = true;
        std::unique_lock<std::mutex> mutexLock(m_mutex);
        if (m_nMaxItems != 0 && m_queue.size() == m_nMaxItems) {
                m_queue.pop_back();
                bRet = false;
        }
        m_queue.push_front(_item);
        m_consumerCondition.notify_one();
        return bRet;
}

template <class T>
bool SharedQueue<T>::ForcePush(const T&& _item)
{
        bool bRet = true;
        std::unique_lock<std::mutex> mutexLock(m_mutex);
        if (m_nMaxItems != 0 && m_queue.size() == m_nMaxItems) {
                m_queue.pop_back();
                bRet = false;
        }
        m_queue.push_front(std::move(_item));
        m_consumerCondition.notify_one();
        return bRet;
}

template <class T>
bool SharedQueue<T>::TryPush(const T& _item)
{
        std::unique_lock<std::mutex> mutexLock(m_mutex);
        if (m_nMaxItems != 0 && m_queue.size() >= m_nMaxItems) {
                return false;
        }
        m_queue.push_front(_item);
        m_consumerCondition.notify_one();
        return true;
}

template <class T>
void SharedQueue<T>::Push(T&& _item)
{
        std::unique_lock<std::mutex> mutexLock(m_mutex);
        if (m_nMaxItems != 0 && m_queue.size() >= m_nMaxItems) {
                m_producerCondition.wait(mutexLock);
        }
        m_queue.push_front(std::move(_item));
        m_consumerCondition.notify_one();
}

template <class T>
bool SharedQueue<T>::IsEmpty()
{
        std::lock_guard<std::mutex> mutexLock(m_mutex);
        return m_queue.empty();
}

template <class T>
void SharedQueue<T>::Clear()
{
        std::lock_guard<std::mutex> mutexLock(m_mutex);
        m_queue.clear();
}

template <class T>
size_t SharedQueue<T>::Size()
{
        std::lock_guard<std::mutex> mutexLock(m_mutex);
        return m_queue.size();
}

template <class T>
size_t SharedQueue<T>::Foreach(const std::function<void(T&)> _lambda)
{
        std::lock_guard<std::mutex> mutexLock(m_mutex);
        for_each(m_queue.begin(), m_queue.end(), _lambda);
        return m_queue.size();
}

template <class T>
void SharedQueue<T>::FindIf(const std::function<bool(T&)> _lambda)
{
        std::lock_guard<std::mutex> mutexLock(m_mutex);
        std::find_if(m_queue.begin(), m_queue.end(), _lambda);
}

template <class T>
void SharedQueue<T>::CriticalSection(const std::function<void(std::deque<T>&)> _lambda)
{
        std::lock_guard<std::mutex> mutexLock(m_mutex);
        _lambda(m_queue);
}

#if 0
#define DebugPCM(filename, p, len) { \
        static FILE *fp; \
        if (fp == NULL) { \
                fp = fopen(filename, "wb+"); \
        } \
        fwrite(p, len, 1, fp); \
        fflush(fp); \
}
#else
#define DebugPCM(...)
#endif

namespace yuv {
        inline void CopyLine(uint8_t *dst, int dstlinesize, const uint8_t *src, int srclinesize, int height) {
                if (dstlinesize == srclinesize) {
                        memcpy(dst, src, srclinesize*height);
                        return;
                }
                int minlinesize = std::min(dstlinesize, srclinesize);
                for (int i = 0; i < height; i++) {
                        memcpy(dst + i*dstlinesize, src + i*srclinesize, minlinesize);
                }
        }
}

static inline std::string newReqId() {
    std::random_device rd;
    const char *dig = "0123456789abcdef";
    char buf[12];
    for (int i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = dig[rd()%16];
    }
    return std::string(buf, sizeof(buf));
}

static inline int fpwrite(FILE *fp, void *buf, size_t len) {
        return fwrite(buf, 1, len, fp) != len ? -1 : 0;
}

class XLogger {
public:
    XLogger() {
        reqid_ = newReqId();
    }

    XLogger(const std::string reqid) {
        reqid_ = reqid;
    }

    std::string reqid_;
};

static inline double now_f() {
        struct timeval  tv;
        struct timezone tz;
        gettimeofday(&tv, &tz);
        return double(tv.tv_sec) + double(tv.tv_usec / 1e6);
}


static inline uint64_t now_ms() {
        return uint64_t(now_f()*1000);
}

static inline bool IsContain(const std::string &str, const std::string &substr) {
        std::string::size_type idx = str.find(substr);
        if (idx != std::string::npos) {
                return true;
        }
        return false;
}

#endif
