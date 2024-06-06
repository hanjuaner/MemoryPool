#ifndef MEMORY_POOL_PAGECACHE_H
#define MEMORY_POOL_PAGECACHE_H

#include "common.h"

// Page Cache:
// 单例：Central Cache获取span的时候，每次都是从同一个page数组中获取span
// 饿汉
class PageCache {
public:
    static PageCache *GetInstance() {
        return &_inst;
    }
private:
    PageCache() {}
    PageCache(const PageCache &) = delete;
    PageCache &operator=(const PageCache &) = delete;
    static PageCache _inst;


public:
//    // 向系统申请获取大对象
//    Span *AllocBigPageObj(size_t size);
//
//    // 释放大对象
//    void FreeBigPageObj(void *ptr, Span *span);
//
    // pc从自己的哈希桶中拿出来一个k页的span
    Span* NewSpan(size_t k);

    // 通过页地址招span
    Span *MapObjectToSpan(void *obj);

    // 管理cc还回来的span
    void ReleaseSpanToPageCache(Span *span);

    void Lock() {
        _pageMtx.lock();
    }

    void UnLock() {
        _pageMtx.unlock();
    }

private:
    SpanList _spanlist[NPAGES];
    std::mutex _pageMtx;
    std::unordered_map<PageID, Span *> _idspanmap;
    ObjectPool<Span> _spanPool;
};

#endif //MEMORY_POOL_PAGECACHE_H