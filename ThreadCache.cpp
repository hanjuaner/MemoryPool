#include "ThreadCache.h"
#include "CentralCache.h"

// tc申请内存
void *
ThreadCache::Allocate(size_t size) {
    assert (size <= MAX_BYTES);
    // 找位置
    size_t index = SizeClass::Index(size);
    FreeList *freelist = &_freelist[index];

    // 自由链表不为空: 直接取
    if (!freelist->Empty()) {
        return freelist->Pop();
    }
        // 自由链表为空：去中心缓存中拿取内存对象，一次取多个防止多次去取而加锁带来的开销
        // 均衡策略:每次中心堆分配给ThreadCache对象的个数是个慢启动策略
        //         随着取的次数增加而内存对象个数增加,防止一次给其他线程分配太多，而另一些线程申请
        //         内存对象的时候必须去PageCache去取，带来效率问题
    else {
        return FetchFromCentralCache(index, SizeClass::RoundUp(size));
    }
}

// tc释放内存
void
ThreadCache::Deallocate(void *ptr, size_t size) {
    assert (size <= MAX_BYTES);
    assert (ptr);

    // 找位置
    size_t index = SizeClass::Index(size);
    FreeList *freelist = &_freelist[index];

    // 直接释放
    freelist->Push(ptr);

    // 满足条件（批量释放）时，释放回中心内存
    if (freelist->Size() >= freelist->MaxSize()) {
        ListTooLong(freelist, size);
    }
}

// tc从cc获取对象
void *
ThreadCache::FetchFromCentralCache(size_t index, size_t size) {
    // 通过MaxSize和NumMoveSize来控制每次从中心缓存获取的内存对象个数
    size_t batchNum = min(_freelist[index].MaxSize(), SizeClass::NumMoveSize(size));
    if (batchNum == _freelist[index].MaxSize()) {
        _freelist[index].MaxSize()++;
    }

    // 输出型参数，返回之后的结果是tc想要的空间
    void *start = nullptr;
    void *end = nullptr;
    // 得到：实际获得的块数（函数返回值），分配回来的空间（[start, end]）
    size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);

    // 把[start, end]这段空间 放到tc对应的链表里
    // 此时tc对应的链表index为空
    // tc分配需要返回空间：所以分配给tc的返回，其他从cc申请到的但是没有分配给tc的直接放进tc不动，所以返回的是[start, end]这段空间的第一个，也就是start
    assert (actualNum >= 1);
    if (actualNum == 1) {
        // 直接返回给线程
        assert (start == end);
        return start;
    } else {
        // start返回给线程，其他插入tc的链表
        _freelist[index].PushRange(NEXT_OBJ(start), end, actualNum - 1);
        return start;
    }
}

// tc释放对象链表过长时，回收到tc
void ThreadCache::ListTooLong(FreeList *freelist, size_t size) {
    void *start = nullptr;
    void *end = nullptr;
    freelist->PopRange(start, end, freelist->MaxSize());
    CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}