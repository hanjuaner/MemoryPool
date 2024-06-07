#ifndef MEMORY_POOL_CONCURRENTALLOC_H
#define MEMORY_POOL_CONCURRENTALLOC_H

#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"

// 线程调用这个函数申请空间
void* ConcurrentAlloc(size_t size);

// 线程调用这个函数回收空间
void* ConcurrentFree(void* obj);

#endif //MEMORY_POOL_CONCURRENTALLOC_H
