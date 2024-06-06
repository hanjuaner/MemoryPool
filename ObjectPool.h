#ifndef MEMORY_POOL_OBJECTPOOL_H
#define MEMORY_POOL_OBJECTPOOL_H

#include <iostream>

using std::cout;
using std::endl;

template<class T>
class ObjectPool {
private:
    char *_memory = nullptr;   // 指向内存块的指针，char因为需要+-操作
    void *_list = nullptr;     // 自由链表的头指针，管理还回来的空间
    size_t _remanentBytes = 0; // 剩余的内存大小
    std::mutex _poolMtx;       // 互斥锁

public:
    T *New() {
        T *obj = nullptr;
        if (_list) {
            void *next = *(void **) _list;
            obj = (T *) _list;
            _list = next;
        } else {
            if (_remanentBytes < sizeof(T)) {
                _remanentBytes = 128 * 1024;
                _memory = (char *) malloc(_remanentBytes);
                if (_memory == nullptr) {
                    throw std::bad_alloc();
                }
            }
        }

        obj = (T *) _memory;
        size_t objSize = sizeof(T) < sizeof(void *) ? sizeof(void *) : sizeof(T);
        _memory += objSize;
        _remanentBytes -= objSize;

        new(obj)T;
        return obj;
    }

    void Delete(T *obj) {
        obj->~T();

        *(void **) obj = _list;
        _list = obj;
    }

    void Lock() {
        _poolMtx.lock();
    }
    void UnLock() {
        _poolMtx.unlock();
    }

};


#endif //MEMORY_POOL_OBJECTPOOL_H
