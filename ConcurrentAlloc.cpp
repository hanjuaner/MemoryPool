#include "ConcurrentAlloc.h"

void* ConcurrentAlloc(size_t size) {
    // cout << std::this_thread::get_id() << " " << pTLSThreadCache << endl;
    if (size > MAX_BYTES) {
        // 直接向os申请
        size_t pageNum = (SizeClass::RoundUp(size)) >> PAGE_SHIFT;

        PageCache::GetInstance()->Lock();
        Span* span = PageCache::GetInstance()->NewSpan(pageNum);
        span->_objsize = size;
        PageCache::GetInstance()->UnLock();

        void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
        return ptr;
    } else {
        // 直接new，因为TLS，所以不存在竞争问题
        if (pTLSThreadCache == nullptr) {
            static ObjectPool<ThreadCache> objPool;
            objPool.Lock();
//            pTLSThreadCache = new ThreadCache;
            pTLSThreadCache = objPool.New();
            objPool.UnLock();
        }
        // 此时，每个线程都有了一个ThreadCache对象

        return pTLSThreadCache->Allocate(size);
    }
}

void* ConcurrentFree(void* obj) {
    assert (obj);
    Span* span = PageCache::GetInstance()->MapObjectToSpan(obj);
    size_t size = span->_objsize;
    if (size > MAX_BYTES) {
        PageCache::GetInstance()->Lock();
        PageCache::GetInstance()->ReleaseSpanToPageCache(span);
        PageCache::GetInstance()->UnLock();
    } else {
        pTLSThreadCache->Deallocate(obj, size);
    }

}