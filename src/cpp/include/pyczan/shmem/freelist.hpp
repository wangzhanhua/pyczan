#ifndef PYCZAN_SHMEM_FREELIST_H_
#define PYCZAN_SHMEM_FREELIST_H_

#include <cstdint>

namespace pyczan {
namespace shmem {

// 块粒度空闲链表分配器
// 最小分配：1 个块（64B），所有分配为连续块
// 释放时自动合并相邻空闲区，碎片不恶化
class FreeListAlloc
{
public:
    static const uint32_t BLOCK_SIZE = 64;

    void Init(void* base, uint32_t totalBlocks, bool clear);
    void* Alloc(uint32_t bytes);
    void  Free(void* ptr);
    bool  IsEmpty() const;
    uint32_t UsedBlocks() const;
    uint32_t TotalBlocks() const;
    uint32_t FreeFragments() const;
    uint32_t BlockSize() const { return BLOCK_SIZE; }

private:
    struct Header {
        uint32_t totalBlocks;
        int32_t  freeHead;  // block index of first free region (-1 = none)
    };

    Header*  m_hdr;
    char*    m_blocks;  // block area start

    uint32_t BlocksNeeded(uint32_t bytes) const;
    void*    BlockPtr(uint32_t idx) const;
    uint32_t BlockIdx(void* ptr) const;

    uint32_t ReadBlocks(uint32_t idx) const;
    void     WriteBlocks(uint32_t idx, uint32_t blocks);
    int32_t  ReadNext(uint32_t idx) const;
    void     WriteNext(uint32_t idx, int32_t next);
    bool     IsFree(uint32_t idx) const;

    // 释放区域的 footer 在 blocks*64-4 处
    uint32_t ReadFooterBlocks(uint32_t idx) const;
    void     WriteFooter(uint32_t idx, uint32_t blocks);
};

} // namespace shmem
} // namespace pyczan

#endif
