#ifndef CPPPROJECT_THREADCACHE_H
#define CPPPROJECT_THREADCACHE_H

#include "common.h"

class ThreadCache {

private:
    FreeList _freelist[NLISTS];   //tc自由链表

public:
    //申请和释放size大小对象
    void* Allocate(size_t size);
    void Deallocate(void* ptr, size_t size);

    //从中心缓存获取对象
    void* FetchFromCentralCache(size_t index, size_t size);
    //释放对象时，链表过长时，回收内存回到中心堆
    void ListTooLong(FreeList* list, size_t size);

};

// 静态TLS
// _declspec (thread)是windows方法
// static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr; // ==> _declspec(thread)是Windows特有的，不是所有编译器都支持
// _declspec (thread) static ThreadCache* tlslist = nullptr;
static thread_local ThreadCache* pTLSThreadCache = nullptr;      // thread_local是C++11提供的，能跨平台

#endif //CPPPROJECT_THREADCACHE_H








