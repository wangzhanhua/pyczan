#ifndef PYCZAN_SHMEM_FREELIST_H_
#define PYCZAN_SHMEM_FREELIST_H_

#include <cstdint>
#include <windows.h>

namespace pyczan { namespace shmem {

// ─────────────────────────────────────────────────────────────
// FreeListAlloc — 块粒度空闲链表分配器
//
// 作用：在共享内存中提供简单可靠的动态内存分配，类似 malloc/free
//
// 分配单位：块（BLOCK_SIZE = 64 字节）
//   每次分配至少 1 个块，分配量 = 向上取整到整数块
//
// 算法：
//   空闲块用单向链表串起来（first-fit）
//   每个空闲块包含：size(4字节) | next(4字节) | ... | footer(4字节)
//   释放时自动合并相邻空闲块，不会出现外碎片恶化
//
// 共享内存布局（每个 Region 中 allocHeader 后紧跟块数据）：
//   [totalBlocks(4) | freeHead(4) | allocLock(4) | allocOwner(4) | blocks(N*64)]
//                                     ↑              ↑
//                                     allocLock/allocOwner 由 Dict 管理
//                                     FreeListAlloc 本身不碰这两个字段
//
// 线程安全：
//   本类不做任何同步，调用方需要在外层加锁
//   当前由 Dict::m_allocLock（SharedSpinLock）保护
//
// 崩溃安全：
//   释放时的前后合并需要读取相邻块的 footer 和 header
//   若共享内存已被破坏，这些读操作可能读到脏数据
//   外层 SharedSpinLock 的 PID 崩溃恢复提供了基本保护
// ─────────────────────────────────────────────────────────────
class FreeListAlloc
{
public:
    static const uint32_t BLOCK_SIZE = 64;

    // Header 总字节数 = totalBlocks(4) + freeHead(4) + allocLock(4) + allocOwner(4)
    static const uint32_t HEADER_SIZE = 16;

    // ── 初始化 ──
    // base: allocator 区域起始地址（指向 Header）
    // totalBlocks: 块总数
    // clear: true=首次初始化，清空并建立初始空闲链表
    void Init(void* base, uint32_t totalBlocks, bool clear);

    // ── 分配 / 释放 ──
    // bytes: 请求的字节数（内部自动向上取整到块边界）
    // 返回: 用户数据指针（偏移 4 字节，前 4 字节是块头部）
    void* Alloc(uint32_t bytes);

    // ptr: Alloc 返回的指针
    void  Free(void* ptr);

    // ── 状态查询 ──
    bool  IsEmpty() const;
    uint32_t UsedBlocks() const;
    uint32_t TotalBlocks() const;
    uint32_t FreeFragments() const;
    uint32_t BlockSize() const { return BLOCK_SIZE; }

    // ── 自旋锁访问（供 Dict 使用） ──
    volatile LONG* LockAddr() const { return &m_hdr->allocLock; }
    DWORD*         OwnerAddr() const { return &m_hdr->allocOwner; }

private:
    // 共享内存中的分配器头部（位于块数据区之前）
    // 注意：allocLock/allocOwner 由 Dict 管理，本类不修改它们
    struct Header {
        uint32_t totalBlocks;
        int32_t  freeHead;                      // 空闲链表头块索引（-1=空）
        volatile LONG allocLock;                // SharedSpinLock（Dict 使用）
        DWORD        allocOwner;                // 自旋锁持有者 PID
    };

    Header*  m_hdr;     // 指向 Header（强制对齐，在共享内存中）
    char*    m_blocks;  // 块数据区起始位置 = m_hdr + sizeof(Header)

    // ── 内部工具 ──
    uint32_t BlocksNeeded(uint32_t bytes) const;
    void*    BlockPtr(uint32_t idx) const;      // 块起始地址
    uint32_t BlockIdx(void* ptr) const;         // 用户数据指针 → 块索引

    uint32_t ReadBlocks(uint32_t idx) const;    // 读取块的 size 字段
    void     WriteBlocks(uint32_t idx, uint32_t blocks);
    int32_t  ReadNext(uint32_t idx) const;      // 读取链表的 next 索引
    void     WriteNext(uint32_t idx, int32_t next);
    bool     IsFree(uint32_t idx) const;        // FREE_FLAG 是否置位

    // Footer 在块数据末尾 4 字节，内容与 header 的 size 字段相同
    // 用于释放时向前合并：通过上一个块的 footer 判断它是否空闲
    uint32_t ReadFooterBlocks(uint32_t idx) const;
    void     WriteFooter(uint32_t idx, uint32_t blocks);
};

}} // namespace pyczan::shmem

#endif
