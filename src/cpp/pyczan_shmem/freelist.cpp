#include "pyczan/shmem/freelist.hpp"
#include <cstring>

namespace pyczan {
namespace shmem {

static const uint32_t FREE_FLAG  = 0x80000000u;
static const uint32_t BLOCK_MASK = 0x7FFFFFFFu;

void FreeListAlloc::Init(void* base, uint32_t totalBlocks, bool clear)
{
    m_hdr = static_cast<Header*>(base);
    m_blocks = static_cast<char*>(base) + sizeof(Header);
    if (!clear) return;

    m_hdr->totalBlocks = totalBlocks;
    m_hdr->freeHead = 0;
    WriteBlocks(0, totalBlocks | FREE_FLAG);
    WriteNext(0, -1);
    WriteFooter(0, totalBlocks | FREE_FLAG);
}

uint32_t FreeListAlloc::BlocksNeeded(uint32_t bytes) const
{
    return (bytes + 4 + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

void* FreeListAlloc::BlockPtr(uint32_t idx) const
{
    return m_blocks + idx * (uint64_t)BLOCK_SIZE;
}

uint32_t FreeListAlloc::BlockIdx(void* ptr) const
{
    return (uint32_t)((static_cast<char*>(ptr) - 4 - m_blocks) / BLOCK_SIZE);
}

uint32_t FreeListAlloc::ReadBlocks(uint32_t idx) const
{
    return *(uint32_t*)BlockPtr(idx);
}
void FreeListAlloc::WriteBlocks(uint32_t idx, uint32_t v) { *(uint32_t*)BlockPtr(idx) = v; }
int32_t FreeListAlloc::ReadNext(uint32_t idx) const { return *(int32_t*)((char*)BlockPtr(idx) + 4); }
void FreeListAlloc::WriteNext(uint32_t idx, int32_t v) { *(int32_t*)((char*)BlockPtr(idx) + 4) = v; }
bool FreeListAlloc::IsFree(uint32_t idx) const { return ReadBlocks(idx) & FREE_FLAG; }

void FreeListAlloc::WriteFooter(uint32_t idx, uint32_t v)
{
    uint32_t blk = v & BLOCK_MASK;
    *(uint32_t*)((char*)BlockPtr(idx) + blk * (uint64_t)BLOCK_SIZE - 4) = v;
}

// 从空闲链表中移除 target
static int32_t ListRemove(int32_t head, int32_t target,
                          int32_t (*rf)(uint32_t), void (*wf)(uint32_t, int32_t))
{
    if (head < 0 || target < 0) return head;
    if (head == target) return rf((uint32_t)target);
    int32_t p = head, c = rf((uint32_t)p);
    while (c >= 0) {
        if (c == target) { wf((uint32_t)p, rf((uint32_t)c)); return head; }
        p = c; c = rf((uint32_t)c);
    }
    return head;
}

// 包装器：static 函数，通过 this 指针调用成员
static int32_t ReadNextWrap(uint32_t idx) { return *(int32_t*)(0); }  // 占位，会被覆盖
// 用模板或宏来避免函数指针问题

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

// ============================================================
// Alloc
// ============================================================

void* FreeListAlloc::Alloc(uint32_t bytes)
{
    uint32_t need = BlocksNeeded(bytes);
    if (need == 0) need = 1;

    int32_t prev = -1;
    int32_t curr = m_hdr->freeHead;

    while (curr >= 0) {
        uint32_t cu = (uint32_t)curr;
        uint32_t rblk = ReadBlocks(cu) & BLOCK_MASK;

        if (rblk >= need) {
            if (rblk >= need + 1) {
                // 分割
                uint32_t rem = rblk - need;
                uint32_t ri = cu + need;
                WriteBlocks(cu, need);    // 分配出去
                WriteFooter(cu, need);
                WriteBlocks(ri, rem | FREE_FLAG);
                WriteNext(ri, ReadNext(cu));
                WriteFooter(ri, rem | FREE_FLAG);
                if (prev < 0) m_hdr->freeHead = (int32_t)ri;
                else WriteNext((uint32_t)prev, (int32_t)ri);
            } else {
                // 精确匹配
                WriteBlocks(cu, need);
                WriteFooter(cu, need);
                if (prev < 0) m_hdr->freeHead = ReadNext(cu);
                else WriteNext((uint32_t)prev, ReadNext(cu));
            }
            return (char*)BlockPtr(cu) + 4;
        }

        prev = curr;
        curr = ReadNext(cu);
    }
    return nullptr;
}

// ============================================================
// Free
// ============================================================

void FreeListAlloc::Free(void* ptr)
{
    if (!ptr) return;

    uint32_t ri = BlockIdx(ptr);
    uint32_t myBlk = ReadBlocks(ri) & BLOCK_MASK;
    uint32_t total = myBlk;

    // 向前合并
    if (ri > 0) {
        uint32_t pf = *(uint32_t*)((char*)m_blocks + ri * (uint64_t)BLOCK_SIZE - 4);
        if (pf & FREE_FLAG) {
            uint32_t pb = pf & BLOCK_MASK;
            uint32_t pi = ri - pb;
            REMOVE_FROM_LIST(m_hdr->freeHead, (int32_t)pi);
            total += pb;
            ri = pi;
        }
    }

    // 向后合并
    uint32_t ni = ri + total;
    if (ni < m_hdr->totalBlocks && IsFree(ni)) {
        uint32_t nb = ReadBlocks(ni) & BLOCK_MASK;
        REMOVE_FROM_LIST(m_hdr->freeHead, (int32_t)ni);
        total += nb;
    }

    WriteBlocks(ri, total | FREE_FLAG);
    WriteNext(ri, m_hdr->freeHead);
    WriteFooter(ri, total | FREE_FLAG);
    m_hdr->freeHead = (int32_t)ri;
}

bool FreeListAlloc::IsEmpty() const { return m_hdr->freeHead < 0; }

uint32_t FreeListAlloc::UsedBlocks() const
{
    uint32_t fb = 0;
    int32_t c = m_hdr->freeHead;
    while (c >= 0) { fb += ReadBlocks((uint32_t)c) & BLOCK_MASK; c = ReadNext((uint32_t)c); }
    return m_hdr->totalBlocks - fb;
}

uint32_t FreeListAlloc::TotalBlocks() const { return m_hdr->totalBlocks; }

uint32_t FreeListAlloc::FreeFragments() const
{
    uint32_t n = 0;
    int32_t c = m_hdr->freeHead;
    while (c >= 0) { n++; c = ReadNext((uint32_t)c); }
    return n;
}

} // namespace shmem
} // namespace pyczan
