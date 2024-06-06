#ifndef MEMORY_POOL_COMMON_H
#define MEMORY_POOL_COMMON_H

#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <unordered_map>
#include <vector>
#include <stdlib.h>
#include <algorithm>
#include <assert.h>
#include "ObjectPool.h"

using std::cout;
using std::endl;
using std::min;

//  size范围                   对齐数          对应哈希桶下标范围
//  [0, 128]                  8B对齐          freelist[0, 16)        8B内存块16个，对应freelist[0], ... , freelist[15]
//  [129, 1024]               16B对齐         freelist[16, 72)       16B内存块56个，对应freelist[16], ... , freelist[71]
//  [1025, 8*1024]            32B对齐         freelist[72, 128)      32B内存块56个，对应freelist[72], ... , freelist[127]
//  [8*1024+1, 64*1024]       64B对齐         freelist[128, 184)     64B内存块56个，对应freelist[128], ... , freelist[183]
//  [64*1024+1, 256*1024]     128B对齐        freelist[184, 208)     128B内存块24个，对应freelist[184], ... , freelist[207]
//  空间浪费率在10%左右

static const size_t MAX_BYTES = 256 * 1024; //ThreadCache 申请的最大内存
static const size_t NLISTS = 208; //数组元素总的有多少个，由对齐规则计算得来
static const size_t PAGE_SHIFT = 13;
static const size_t NPAGES = 129;

// 访问和修改链表的指针
inline static void *&NEXT_OBJ(void *obj) {    //返回类型是对 void* 类型的引用
    return *((void **) obj);   //  将obj强转为void**类型（指向 void* 类型的指针），再解引用（访问指针所指向的地址上存储的值）这个类型的指针，得到void*类型的引用
}

// 链表，用于存储内存对象
class FreeList {
private:
    void *_list = nullptr;  // 链表头指针
    size_t _size = 0;       // 链表中的内存对象个数
    size_t _maxsize = 1;    // 链表中最大的内存对象个数

public:
    // 进栈 (释放内存进freelist)
    void Push(void *obj) {   // 头插
        assert (obj);        // obj不为空

        NEXT_OBJ(obj) = _list;  //   obj -> next = _list
        _list = obj;            //  _list = obj -> next(end)
        ++_size;
    }

    void PushRange(void *start, void *end, size_t size) {
        NEXT_OBJ(end) = _list;  // start ... end -> next = _list
        _list = start;          // _list = start ... end -> next

        _size += size;          // size个内存块
    }

    // 出栈 （申请内存出freelist）
    void *Pop() {          // 头删
        assert (_list);    // _list不为空

        void *obj = _list;      // obj 指向链表头
        _list = NEXT_OBJ(obj);  // 现链表头往前一个
        --_size;
        return obj;             // 原链表头取出
    }

    void PopRange(void *&start, void *&end, size_t n) {
        assert (n <= _size);
        start = end = _list;
        for (size_t i = 0; i < n - 1; ++i) {
            end = NEXT_OBJ(end);
        }
        _list = NEXT_OBJ(end);
        NEXT_OBJ(end) = nullptr;
        _size -= n;
    }


    bool Empty() {
        return _list == nullptr;
    }

    size_t Size() {
        return _size;
    }

    size_t &MaxSize() {
        return _maxsize;
    }
};

// 对齐大小的设计（对齐规则）
class SizeClass {
public:
    // 大佬写法，也可以用%和?:来实现
    // size: 开辟内存块大小
    // align：内存块应该按多少字节对齐，3：按 2的3次方 = 8字节 对齐
    inline static size_t _Index(size_t size, size_t align) {
        size_t alignnum = 1 << align;
        return ((size + alignnum - 1) >> align) - 1;
    }

    inline static size_t _Roundup(size_t size, size_t align) {
        size_t alignnum = 1 << align;  // 计算对齐的数值，等价于 2^align
        return (size + alignnum - 1) & ~(alignnum - 1);
    }

public:
    // 计算对应的自由链表下标（对应哪个哈希桶）
    inline static size_t Index(size_t size) {
        assert (size <= MAX_BYTES);
        static int group_array[4] = {16, 56, 56, 56};
        if (size < 128) {
            return _Index(size, 3);
        } else if (size < 1024) {
            return _Index(size - 128, 4) + group_array[0];
        } else if (size < (8 * 1024)) {
            return _Index(size - 1024, 7) + group_array[0] + group_array[1];
        } else if (size < (64 * 1024)) {
            return _Index(size - 8 * 1024, 10) + group_array[0] + group_array[1] + group_array[2];
        } else if (size < (256 * 1024)) {
            return _Index(size - 64 * 1024, 13) + group_array[0] + group_array[1] + group_array[2] + group_array[3];
        } else {
            assert (false);
        }
        return -1;
    }

    // 计算对齐后的字节数
    inline static size_t RoundUp(size_t bytes) {
        if (bytes <= 128) {
            return _Roundup(bytes, 3);
        } else if (bytes <= 1024) {
            return _Roundup(bytes, 4);
        } else if (bytes <= 8 * 1024) {
            return _Roundup(bytes, 7);
        } else if (bytes <= 64 * 1024) {
            return _Roundup(bytes, 10);
        } else if (bytes <= 256 * 1024) {
            return _Roundup(bytes, 13);
        } else {
            // 单次申请空间超过256k
            return _Roundup(bytes, PAGE_SHIFT);
        }
    }

    // 申请上限算法：最多512个，最少2个
    static size_t NumMoveSize(size_t size) {
        assert (size > 0);

        int num = MAX_BYTES / size;
        if (num < 2) {
            num = 2;
        }
        if (num > 512) {
            num = 512;
        }
        return num;
    }

    // 块页匹配算法（size对应page的数量）
    static size_t NumMovePage(size_t size) {
        // 当cc中没有span为tc提供小块空间时，cc就需要向pc申请一块span，此时需要根据一块空间的大小来匹配
        // 出一个维护页空间较为合适的span，以保证span为size后尽量不浪费或不足够还再频繁申请相同大小的span
        size_t num = NumMoveSize(size);   // cc一次给tc num个内存块
        size_t npage = num * size;        // 这些内存块所占总空间npage
        npage >>= PAGE_SHIFT;             // 对应页数npage
        if (npage == 0) {                 // 最少给1页
            npage = 1;
        }
        return npage;
    }
};

typedef size_t PageID;

// Span：内存页
// Span是一个跨度，既可以分配内存出去，也是负责将内存回收回来到PageCache合并
// 是一链式结构，定义为结构体就行，避免需要很多的友元
struct Span {
    PageID _pageid = 0; // 页号
    size_t _npage = 0;  // 页数（span管理了多少页）

    Span *_prev = nullptr;  // 前一个Span
    Span *_next = nullptr;  // 后一个Span

    void *_list = nullptr;  // 链表头指针
    size_t _objsize = 0;    // 内存块大小

    size_t _usecount = 0;   // 使用计数(span分配出去的内存块个数)
    bool _isUse = false;    // span是否被使用，false：未被使用，在pc中；true：被使用，在cc中
};

// Span链表，双向循环
class SpanList {
private:
    Span *_head;   // 哨兵位头节点
    std::mutex _mutex;  // 互斥锁

public:
    SpanList() {
        _head = new Span;        // 哨兵位头节点
        _head->_next = _head;    // 双向循环
        _head->_prev = _head;
    }

    ~SpanList() {
        Span *cur = _head->_next;
        while (cur != _head) {
            Span *next = cur->_next;
            delete cur;
            cur = next;
        }
        delete _head;
        _head = nullptr;
    }

    // 锁死拷贝
    SpanList(const SpanList &) = delete;

    SpanList &operator=(const SpanList &) = delete;

    Span *Begin() {   // 第一个
        return _head->_next;
    }

    Span *End() {    // 最后一个的下一个
        return _head;
    }

    bool Empty() {
        return _head->_next == _head;
    }

    //在cur前面插入一个newspan
    void Insert(Span *cur, Span *newspan) {
        assert (cur);
        assert (newspan);

        Span *prev = cur->_prev;

        cur->_prev = newspan;
        prev->_next = newspan;
        newspan->_prev = prev;
        newspan->_next = cur;
    }

    //删除cur，只把cur拿出来，没有释放掉 (因为cur节点的span需要回收，而不是释放)
    void Erase(Span *cur) {
        assert (cur);
        assert (cur != _head);

        Span *next = cur->_next;
        Span *prev = cur->_prev;

        next->_prev = prev;
        prev->_next = next;
    }

    //尾插
    void PushBack(Span *newspan) {
        Insert(End(), newspan);
    }

    //头插
    void PushFront(Span *newspan) {
        Insert(Begin(), newspan);
    }

    //尾删,只把尾部拿出来，没有释放掉
    Span *PopBack() {
        Span *span = _head->_prev;
        Erase(span);
        return span;
    }

    //头删,只把头部拿出来，没有释放掉
    Span *PopFront() {
        Span *span = _head->_next;
        Erase(span);
        return span;
    }

    // 加锁
    void Lock() {
        _mutex.lock();
    }

// 释放锁
    void Unlock() {
        _mutex.unlock();
    }
};

#ifdef _WIN32
#include <Windows.h>
#else

#include <unistd.h>
#include <sys/mman.h>
#include <unistd.h>

#endif

// 堆上申请空间
inline static void *SystemAlloc(size_t kpage) {
    void *ptr = nullptr;
#ifdef _WIN32
    ptr = VirtualAlloc(0, kpage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    ptr = mmap(0, kpage << PAGE_SHIFT, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
#endif
    if (ptr == nullptr)
        throw std::bad_alloc();
    return ptr;
}

// 堆上释放空间
inline static void SystemFree(void *ptr) {
#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, 0);
#endif
}

#endif //MEMORY_POOL_COMMON_H
