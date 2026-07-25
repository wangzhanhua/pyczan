#ifndef PYCZAN_SHMEM_BITMAP_H_
#define PYCZAN_SHMEM_BITMAP_H_

#include <cstdint>

namespace pyczan {
namespace shmem {

// 位图分配器：管理固定大小块的空闲/已用状态
class BitmapAllocator
{
public:
    BitmapAllocator();

    // 初始化：绑定到共享内存中的位图区域
    // bitmapBase: 位图起始地址（按 8 字节对齐）
    // totalBlocks: 管理的总块数
    void Init(void* bitmapBase, uint32_t totalBlocks);

    // 分配一个空闲块，返回块编号（1-based，0 表示无空间）
    uint32_t Allocate();

    // 释放一个块
    void Free(uint32_t blockIndex);

    // 批量分配 count 个块，返回首块编号
    // 各块之间不一定连续，调用者需通过 nextBlock 链接
    uint32_t AllocateChain(uint32_t count);

    // 批量释放链中的全部块
    void FreeChain(uint32_t firstBlock);

    // 检查块是否空闲
    bool IsFree(uint32_t blockIndex) const;

    // 统计信息
    uint32_t UsedCount() const;
    uint32_t FreeCount() const;
    uint32_t TotalBlocks() const { return m_totalBlocks; }

private:
    uint64_t* m_bitmap;
    uint32_t  m_totalBlocks;
    uint32_t  m_qwords;

    void SetBit(uint32_t index, bool used);
    bool GetBit(uint32_t index) const;
};

} // namespace shmem
} // namespace pyczan

#endif // PYCZAN_SHMEM_BITMAP_H_
