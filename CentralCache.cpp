#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_inst;

// 让cc拿到一个spanlist下非空的span
Span *
CentralCache::GetOneSpan(SpanList &spanlist, size_t size) {
    // 1. 判断cc对应index下挂的有没有管理非空空间的span，
    // 2. 有：将该span返回
    // 3. 没有：向pc申请新的span（调用NewSpan即可）

    Span *it = spanlist.Begin();
    while (it != spanlist.End()) {
        if (it->_list != nullptr)
            return it;
        else
            it = it->_next;
    }

    // cc中有非空span已经在上面return了，没有return的话，说明cc中没有非空span，下面向pc中申请
    // 解锁cc1：cc中没有非空span
    spanlist.Unlock();
    size_t k = SizeClass::NumMovePage(size);

    // pc加锁解锁1：cc向pc申请是span
    PageCache::GetInstance()->Lock();
    Span *span = PageCache::GetInstance()->NewSpan(k);    // 此时的span还没有被划分
    span->_isUse = true;
    span->_objsize = size;
    PageCache::GetInstance()->UnLock();

    // 划分span：
    // NewSpan实现：返回的span中的_pageID（页号）和_npage（管理页数）
    // 获得span所管理空间的首地址start = 页号 * 单页大小
    // end = start + 这个span大小（页数 * 页大小）
    // 把span划分为若干个size大小内存块的链表，并放到_freeList后面
    // 定义一个tail表示当前已经划分过的末尾位置
    // tail的初始位置就应该是start，然后不断让tail往后挪动，并链接tail后面的块空间
    // 直到tail挪到end的位置
    // 最后将tail的next指向nullptr
    // 将这块划分好的空间放到span的_freeList中
    char *start = (char *) (span->_pageid << PAGE_SHIFT);
    char *end = (char *) (start + (span->_npage << PAGE_SHIFT));
    span->_list = start;
    void *tail = start;

    start += size;
    while (start < end) {
        NEXT_OBJ(tail) = start;
        start += size;
        tail = NEXT_OBJ(tail);
    }
    NEXT_OBJ(tail) = nullptr;
    // 获得了划分好的span，但该span不在对应的spanlist中

    // cc加锁2：把切好的span挂到cc中去时
    spanlist.Lock();
    spanlist.PushFront(span);
    return span;
}

// 给thread cache一定数量的对象
size_t
CentralCache::FetchRangeObj(void *&start, void *&end, size_t batchNum, size_t size) {
    // 找size对应的spanlist
    size_t index = SizeClass::Index(size);
    SpanList &spanlist = _spanlist[index];

    // _spanlist[index]所挂载的span的情况：
    // 1. 有span且span所管理的空间不为空：直接获取到这个管理空间非空的span即可。
    // 2. 有span但span所管理的空间为空：向pc申请一个新的span。
    // 2. 没有span：向pc申请一个新的span。

    // cc加锁1：取cc自己哈希桶中的span时
    spanlist.Lock();

    // 获得一个当前spanlist下，有挂载内存块的span
    Span *span = GetOneSpan(spanlist, size);
    assert (span);
    assert (span->_list);

    // 从上面的span中取batchNum个size内存块，有batchNum就取batchNum个，没有就能去多少取多少

    // start指向list（首）
    // end指向list，向后挪，直到batchNum-1（尾） 或 end的next为空，记录end挪了多少块作为函数返回值
    // span->_list指向end的next
    // end的next指向nullptr（如果不为空的话）
    start = end = span->_list;
    size_t i = 0;
    size_t actualNum = 1;
    while (NEXT_OBJ(end) != nullptr && i < batchNum - 1) {
        end = NEXT_OBJ(end);
        ++i;
        ++actualNum;
    }
    span->_list = NEXT_OBJ(end);
    span->_usecount += actualNum;
    NEXT_OBJ(end) = nullptr;

    // cc解锁2：切分后的span交给需要的tc之后
    spanlist.Unlock();

    return actualNum;
}

void
CentralCache:: ReleaseListToSpans(void *start, size_t size) {
    size_t index = SizeClass::Index(size);
    SpanList& spanlist = _spanlist[index];

    // cc加锁3
    spanlist.Lock();

    while (start) {
        Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
        void* next = NEXT_OBJ(start);
        NEXT_OBJ(start) = span->_list;
        span->_list = start;
        span->_usecount -- ;
        if (span->_usecount == 0) {
            spanlist.Erase(span);
            span->_list = nullptr;
            span->_next = nullptr;
            span->_prev = nullptr;

            // 归还span，解锁4
            spanlist.Unlock();

            // pc加锁解锁2：cc归还span给pc
            PageCache::GetInstance()->Lock();
            PageCache::GetInstance()->ReleaseSpanToPageCache(span);
            PageCache::GetInstance()->UnLock();

            // 归还完毕，加锁4
            spanlist.Lock();
        }
        start = next;
    }

    // cc解锁3
    spanlist.Unlock();
}

