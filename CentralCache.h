#ifndef MEMORY_POOL_CENTRALCACHE_H
#define MEMORY_POOL_CENTRALCACHE_H

#include "common.h"

// ThreadCache:
// 资源过剩时，回收当前ThreadCache内部的的内存，分配给其他ThreadCache
// 只有一个中心缓存：所有的线程在一个中心缓存获取内存，所以中心缓存可以使用单例模式创建类
// 要加锁

// 单例，懒汉
class CentralCache {
public:
    // 获取唯一实例
    static CentralCache *GetInstance() {
        return &_inst;
    }

    // 让cc拿到一个spanlist下非空的span
    // cc有非空span：将该span返回
    // cc没有非空span：向pc申请新的span
    Span *GetOneSpan(SpanList &spanlist, size_t size);

    // 给thread cache一定数量的对象
//    从CentralCache对应index下标的哈希桶中拿出batchNum块大小为Size的块空间
//    这个函数应该返回来大小为 batchNum * Size 的一段空间，而这一段空间就是从对应index的SpanList中挑出一个Span，然后再从Span中挑出大小为 batchNum * Size 的一段空间。
//    有可能会出现Span中空间不足以提供这么多的情况（单个Span中的小块空间可能是被多个tc拿走的，那么就可能出现某个tc要的时候不够的情况，此时有多少就给多少，或者是完全没有的时候cc就会去找pc要）。
//    所以此时就算不够还是要返回一段空间的，那么如何确定返回了多少块呢？
//    规定一下返回值返回的是实际提供的大小为Size的空间的块数，并且应该给两个指针的参数，一个void* start，一个void* end，用来划定cc所提供的空间的开始和结尾，所以这个函数声明应该长这样：
    size_t FetchRangeObj(void *&start, void *&end, size_t batchNum, size_t size);
    // start、end：输出型参数，cc提供的空间的开始结尾
    // n：tc需要多少块size大小的空间
    // size：tc需要的单块空间的大小
    // 返回值：cc世纪提供的空间大小

    // 将tc还回来的多块空间放到span中
    void ReleaseListToSpans(void *start, size_t size);
//
private:
    SpanList _spanlist[NLISTS];     // cc中挂载的spanlist

// 确保唯实例是'_inst'
private:
    // 构造函数私有，防止外部代码创建实例
    CentralCache() {}
    // 饿汉创建一个CentralCache对象
    static CentralCache _inst;

    // 禁止使用拷贝函数
    CentralCache(CentralCache &) = delete;
    CentralCache& operator=(CentralCache &) = delete;
};


#endif //MEMORY_POOL_CENTRALCACHE_H
