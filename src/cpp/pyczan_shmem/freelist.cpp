#include "pyczan/shmem/freelist.hpp"
#include <cstring>

namespace pyczan { namespace shmem {

// FREE_FLAG 标记一个块是否空闲（在 size 字段的最高位）
// BLOCK_MASK 用于提取实际的 size 值
static const uint32_t FREE_FLAG = 0x80000000u;
static const uint32_t BLOCK_MASK = 0x7FFFFFFFu;

// ─────────────────────────────────────────────────────────────
// Init — 初始化分配器
//
// 布局：
//   base → [Header: totalBlocks(4) | freeHead(4) | allocLock(4) | allocOwner(4)]
//   m_blocks → [block0] [block1] ... [blockN]
//
// clear=true 时：
//   建立初始空闲链表：1 个空闲大块 = 所有块都空闲
//   block0 的 size = totalBlocks | FREE_FLAG
//   block0 的 next = -1（链表结束）
//   block0 的 footer = totalBlocks | FREE_FLAG
// ─────────────────────────────────────────────────────────────
void FreeListAlloc::Init(void* base, uint32_t totalBlocks, bool clear)
{
    m_hdr = static_cast<Header*>(base);
    m_blocks = static_cast<char*>(base) + sizeof(Header);

    if (!clear) return;  // 已有数据，不需要初始化

    m_hdr->totalBlocks = totalBlocks;
    m_hdr->freeHead = 0;                     // 链表头指向第一个块

    // 初始状态：整个区域是一个大空闲块
    WriteBlocks(0, totalBlocks | FREE_FLAG);  // size = totalBlocks, free
    WriteNext(0, -1);                          // next = -1（链表尾）
    WriteFooter(0, totalBlocks | FREE_FLAG);  // footer = header 的拷贝
}

// ─────────────────────────────────────────────────────────────
// BlocksNeeded — 计算分配所需的块数
//
// 公式：(bytes + 4 + BLOCK_SIZE - 1) / BLOCK_SIZE
//   +4:  每个分配在用户数据前有 4 字节头部（存块数）
//   +BLOCK_SIZE-1: 向上取整
//
// 例如：bytes=1 → (1+4+63)/64 = 68/64 = 1 块
//       bytes=64 → (64+4+63)/64 = 131/64 = 2 块（1 块 header + 64 数据）
// ─────────────────────────────────────────────────────────────
uint32_t FreeListAlloc::BlocksNeeded(uint32_t bytes) const
{
    return (bytes + 4 + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

// ── 地址转换工具 ──

// 块索引 → 块起始地址（不含用户数据的 4 字节偏移）
void* FreeListAlloc::BlockPtr(uint32_t idx) const
{
    return m_blocks + idx * (uint64_t)BLOCK_SIZE;
}

// 用户数据指针（Alloc 返回值）→ 块索引
uint32_t FreeListAlloc::BlockIdx(void* ptr) const
{
    // ptr 指向用户数据，往前 4 字节是块头部
    return (uint32_t)((static_cast<char*>(ptr) - 4 - m_blocks) / BLOCK_SIZE);
}

// ── 块元数据读写 ──

// 每个块前 4 字节 = size（含 FREE_FLAG）
uint32_t FreeListAlloc::ReadBlocks(uint32_t idx) const
{
    return *(uint32_t*)BlockPtr(idx);
}
void FreeListAlloc::WriteBlocks(uint32_t idx, uint32_t v)
{
    *(uint32_t*)BlockPtr(idx) = v;
}

// 每个块 4-8 字节 = next（空闲链表的后继索引）
int32_t FreeListAlloc::ReadNext(uint32_t idx) const
{
    return *(int32_t*)((char*)BlockPtr(idx) + 4);
}
void FreeListAlloc::WriteNext(uint32_t idx, int32_t v)
{
    *(int32_t*)((char*)BlockPtr(idx) + 4) = v;
}

// 判断块是否空闲（FREE_FLAG 置位）
bool FreeListAlloc::IsFree(uint32_t idx) const
{
    return ReadBlocks(idx) & FREE_FLAG;
}

// Footer = 块的末尾 4 字节（内容与 Header 的 size 相同）
// 用于 Free 时向前合并：当前块的前一个块的 footer 标记它是否空闲
void FreeListAlloc::WriteFooter(uint32_t idx, uint32_t v)
{
    uint32_t blk = v & BLOCK_MASK;
    *(uint32_t*)((char*)BlockPtr(idx) + blk * (uint64_t)BLOCK_SIZE - 4) = v;
}

// ── 从空闲链表中移除指定节点（宏，用于合并操作） ──
#define REMOVE_FROM_LIST(head, target) do { \
    int32_t _h = (head), _t = (target); \
    if (_h >= 0 && _t >= 0) { \
        if (_h == _t) { (head) = ReadNext((uint32_t)_t); } \
        else { \
            int32_t _p = _h; \
            int32_t _c = ReadNext((uint32_t)_p); \
            while (_c >= 0) { \
                if (_c == _t) { WriteNext((uint32_t)_p, ReadNext((uint32_t)_c)); break; } \
                _p = _c; _c = ReadNext((uint32_t)_c); \
            } \
        } \
    } \
} while(0)

// ─────────────────────────────────────────────────────────────
// Alloc — 分配连续块
//
// 算法：first-fit
//   1. 从头遍历空闲链表，找到第一个 size >= need 的块
//   2. 精确匹配：从链表中移除该块，标记为已分配
//   3. 大于 need+1：分割成已分配块 + 剩余空闲块
//   4. 找不到 → 返回 nullptr
//
// 分割规则：
//   如果 rblk >= need + 1（至少多 1 个块），才做分割
//   否则直接分配整块（避免产生不可用的碎片块）
// ─────────────────────────────────────────────────────────────
void* FreeListAlloc::Alloc(uint32_t bytes)
{
    uint32_t need = BlocksNeeded(bytes);
    if (need == 0) need = 1;

    int32_t prev = -1;
    int32_t curr = m_hdr->freeHead;

    while (curr >= 0)
    {
        uint32_t cu = (uint32_t)curr;
        uint32_t rblk = ReadBlocks(cu) & BLOCK_MASK;

        if (rblk >= need)
        {
            if (rblk >= need + 1)
            {
                // ── 分割：前半部分分配出去，后半部分继续空闲 ──
                uint32_t rem = rblk - need;
                uint32_t ri = cu + need;  // 剩余块的起始索引

                // 写分配出去的块的头部
                WriteBlocks(cu, need);
                WriteFooter(cu, need);

                // 写剩余空闲块的头部
                WriteBlocks(ri, rem | FREE_FLAG);
                WriteNext(ri, ReadNext(cu));
                WriteFooter(ri, rem | FREE_FLAG);

                // 更新链表：将当前块替换为剩余块
                if (prev < 0)
                    m_hdr->freeHead = (int32_t)ri;
                else
                    WriteNext((uint32_t)prev, (int32_t)ri);
            }
            else
            {
                // ── 精确匹配或相差 1 块：直接分配整个块 ──
                WriteBlocks(cu, need);
                WriteFooter(cu, need);

                // 从链表中移除
                if (prev < 0)
                    m_hdr->freeHead = ReadNext(cu);
                else
                    WriteNext((uint32_t)prev, ReadNext(cu));
            }

            // 返回用户数据指针（块起始 + 4 字节头部偏移）
            return (char*)BlockPtr(cu) + 4;
        }

        prev = curr;
        curr = ReadNext(cu);
    }

    return nullptr;  // 没有足够大的空闲块
}

// ─────────────────────────────────────────────────────────────
// Free — 释放之前 Alloc 返回的指针
//
// 做了三件事：
//   1. 向前合并：检查前一个块（通过 footer）是否空闲
//      如果是，从前面的块开始合并，从链表中移除前面的块
//   2. 向后合并：检查后一个块（通过 header）是否空闲
//      如果是，合并进来的，从链表中移除后一个块
//   3. 将合并后的大块插回空闲链表头部
// ─────────────────────────────────────────────────────────────
void FreeListAlloc::Free(void* ptr)
{
    if (!ptr) return;

    uint32_t ri = BlockIdx(ptr);
    uint32_t myBlk = ReadBlocks(ri) & BLOCK_MASK;
    uint32_t total = myBlk;

    // ── 向前合并 ──
    // 检查前一块的 footer（在 ri*BLOCK_SIZE - 4 处）
    if (ri > 0)
    {
        uint32_t pf = *(uint32_t*)((char*)m_blocks + ri * (uint64_t)BLOCK_SIZE - 4);
        if (pf & FREE_FLAG)
        {
            uint32_t pb = pf & BLOCK_MASK;
            uint32_t pi = ri - pb;            // 前一块的起始索引
            REMOVE_FROM_LIST(m_hdr->freeHead, (int32_t)pi);
            total += pb;
            ri = pi;                           // 合并后从 pi 开始
        }
    }

    // ── 向后合并 ──
    uint32_t ni = ri + total;
    if (ni < m_hdr->totalBlocks && IsFree(ni))
    {
        uint32_t nb = ReadBlocks(ni) & BLOCK_MASK;
        REMOVE_FROM_LIST(m_hdr->freeHead, (int32_t)ni);
        total += nb;
    }

    // ── 写回合并后的大块，插回空闲链表头部 ──
    WriteBlocks(ri, total | FREE_FLAG);
    WriteNext(ri, m_hdr->freeHead);
    WriteFooter(ri, total | FREE_FLAG);
    m_hdr->freeHead = (int32_t)ri;
}

// ── 状态查询 ──

bool FreeListAlloc::IsEmpty() const
{
    return m_hdr->freeHead < 0;
}

uint32_t FreeListAlloc::UsedBlocks() const
{
    uint32_t fb = 0;
    int32_t c = m_hdr->freeHead;
    while (c >= 0)
    {
        fb += ReadBlocks((uint32_t)c) & BLOCK_MASK;
        c = ReadNext((uint32_t)c);
    }
    return m_hdr->totalBlocks - fb;
}

uint32_t FreeListAlloc::TotalBlocks() const
{
    return m_hdr->totalBlocks;
}

uint32_t FreeListAlloc::FreeFragments() const
{
    uint32_t n = 0;
    int32_t c = m_hdr->freeHead;
    while (c >= 0)
    {
        n++;
        c = ReadNext((uint32_t)c);
    }
    return n;
}

} // namespace shmem
} // namespace pyczan
