#include "PageCache.h"

#include <sys/mman.h>
#include <unistd.h>


PageCache PageCache::_inst;

//// 向系统申请获取大对象
//Span *
//PageCache::AllocBigPageObj(size_t size) {
//    assert (size > MAX_BYTES);
//
//    size = SizeClass::_Roundup(size, PAGE_SHIFT); // 对齐
//    size_t npage = size >> PAGE_SHIFT; // 页数
//    if (npage < NPAGES) {
//        Span *span = NewSpan(npage);
//        span->_objsize = size;
//        return span;
//    } else {
//        const size_t MMAP_THRESHOLD = 128 * 1024; // 128KB 阈值
//        size_t memorySize = npage << PAGE_SHIFT;
//        void *ptr = nullptr;
//        if (memorySize < MMAP_THRESHOLD) {
//            ptr = sbrk(memorySize);
//            if (ptr == (void *) -1) {     // sbrk 返回的错误值是 (void*)-1
//                ptr = nullptr;
//            }
//        } else {
//            ptr = mmap(NULL, memorySize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
//            if (ptr == MAP_FAILED) {
//                ptr = nullptr;
//            }
//        }
//
//        if (ptr == nullptr)
//            throw std::bad_alloc();
//
//
//        Span *span = new Span;
//        span->_objsize = npage << PAGE_SHIFT;
//        span->_npage = npage;
//        span->_pageid = (PageID) ptr >> PAGE_SHIFT;
//        _idspanmap[span->_pageid] = span;
//
//        return span;
//    }
//}
//
//// 释放大对象
//void
//PageCache::FreeBigPageObj(void *ptr, Span *span) {
//    size_t npage = span->_objsize >> PAGE_SHIFT;
//    if (npage < NPAGES) {
//        span->_objsize = 0;
//        ReleaseSpanToPageCache(span);
//    } else {
//        _idspanmap.erase(span->_pageid);
//        delete span;
//        // ?
//        if (span->_objsize < 128 * 1024) {
//            sbrk(-span->_objsize);
//        } else {
//            munmap(ptr, npage << PAGE_SHIFT);
//        }
//    }
//}

// pc从自己的哈希桶中拿出来一个k页的span
// k：申请的页数
Span *
PageCache::NewSpan(size_t k) {
    // pc检查自己第k个桶里面有没有span
    // 情况1：有：返回一个span
    // 情况2：没有：往下找更大页的桶中的span，拿出来拆开
    // 如：id（起始页号） + n（管理页数）的span 拆成id + k 和 id + k  +  n - k 两个span
    // 情况3：还没有：向系统申请一个128页的span并切分，返回所需要的k页span
    // 情况4：单次申请超过128页：直接向系统申请

    assert (k > 0);

    // 情况4
    if (k > NPAGES - 1) {
        void *ptr = SystemAlloc(k);
        Span *span = new Span;
//        Span *span = _spanPool.New();
        span->_pageid = ((PageID) ptr) >> PAGE_SHIFT;
        span->_npage = k;
        _idspanmap[span->_pageid] = span;
        _idspanmap[span->_pageid + span->_npage] = span;
        return span;
    }

    // 情况1
    if (!_spanlist[k].Empty()) {
        Span *span = _spanlist[k].PopFront();
        for (PageID i = 0; i < span->_npage; ++i) {
            _idspanmap[span->_pageid + i] = span;
        }
        return span;
    }

    // 情况2
    for (size_t i = k + 1; i < NPAGES; ++i) {
        if (!_spanlist[i].Empty()) {
            Span *nSpan = _spanlist[i].PopFront();
            Span *kSpan = new Span;
//            Span *kSpan = _spanPool.New();

            kSpan->_pageid = nSpan->_pageid;
            kSpan->_npage = k;

            nSpan->_pageid += k;
            nSpan->_npage -= k;

            kSpan->_pageid = nSpan->_pageid;
            kSpan->_npage = k;

            _spanlist[nSpan->_npage].PushFront(nSpan);

            _idspanmap[nSpan->_pageid] = nSpan;
            _idspanmap[nSpan->_pageid + nSpan->_npage - 1] = nSpan;

            for (PageID i = 0; i < kSpan->_npage; ++i) {
                _idspanmap[kSpan->_pageid + i] = kSpan;
            }
            return kSpan;
        }
    }

    // 情况3
    void *ptr = SystemAlloc(NPAGES - 1);
    Span *bigSpan = new Span;
//    Span *bigSpan = _spanPool.New();
    bigSpan->_pageid = (((PageID) ptr) >> PAGE_SHIFT);
    bigSpan->_npage = NPAGES - 1;
    _spanlist[bigSpan->_npage].PushFront(bigSpan);

//    _idspanmap[bigSpan->_pageid] = bigSpan;
//    _idspanmap[bigSpan->_pageid + bigSpan->_npage - 1] = bigSpan;
    return NewSpan(k);
}

//获取从对象到span的映射
Span *
PageCache::MapObjectToSpan(void *obj) {
    PageID id = (((PageID) obj) >> PAGE_SHIFT);
    std::unique_lock<std::mutex> lock(_pageMtx);          // 智能锁
    auto it = _idspanmap.find(id);
    if (it != _idspanmap.end()) {
        return it->second;
    } else {
        assert(false);
        return nullptr;
    }
}

// 管理cc还回来的span
void
PageCache::ReleaseSpanToPageCache(Span *span) {
    if (span->_npage > NPAGES - 1) {
        void *ptr = (void *) (span->_pageid << PAGE_SHIFT);
        SystemFree(ptr);
        delete span;
//        _spanPool.Delete(span);
        return;
    }
    // 向左不断合并
    while (1) {
        PageID leftId = span->_pageid - 1;
        auto it = _idspanmap.find(leftId);

        // 没有相邻span，停止合并
        if (it == _idspanmap.end()) {
            break;
        }

        Span *leftSpan = it->second;
        // 相邻span在cc中，停止合并
        if (leftSpan->_isUse) {
            break;
        }
        // 合并后>128页，停止合并
        if (leftSpan->_npage + span->_npage > NPAGES - 1) {
            break;
        }
        // 合并
        span->_npage += leftSpan->_npage;
        span->_pageid = leftSpan->_pageid;
        _spanlist[leftSpan->_npage].Erase(leftSpan);
        delete leftSpan;
//        _spanPool.Delete(leftSpan);
    }
    // 向右不断合并
    while (1) {
        PageID rightId = span->_pageid + span->_npage;
        auto it = _idspanmap.find(rightId);

        // 没有相邻span，停止合并
        if (it == _idspanmap.end()) {
            break;
        }

        Span *rightSpan = it->second;
        // 相邻span在cc中，停止合并
        if (rightSpan->_isUse) {
            break;
        }
        // 合并后>128页，停止合并
        if (rightSpan->_npage + span->_npage > NPAGES - 1) {
            break;
        }
        // 合并
        span->_npage += rightSpan->_npage;
        _spanlist[rightSpan->_npage].Erase(rightSpan);
        delete rightSpan;
//        _spanPool.Delete(rightSpan);
    }

    // 合并完成，将当前span挂到桶中
    _spanlist[span->_npage].PushFront(span);
    span->_isUse = false;

    _idspanmap[span->_pageid] = span;
    _idspanmap[span->_pageid + span->_npage - 1] = span;
}