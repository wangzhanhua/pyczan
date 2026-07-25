#include "pyczan/shmem/bitmap.hpp"
#include <intrin.h>

namespace pyczan {
namespace shmem {

BitmapAllocator::BitmapAllocator()
    : m_bitmap(nullptr)
    , m_totalBlocks(0)
    , m_qwords(0)
{
}

void BitmapAllocator::Init(void* bitmapBase, uint32_t totalBlocks)
{
    m_bitmap = static_cast<uint64_t*>(bitmapBase);
    m_totalBlocks = totalBlocks;
    m_qwords = (totalBlocks + 63) / 64;
}

uint32_t BitmapAllocator::Allocate()
{
    if (m_bitmap == nullptr) return 0;

    for (uint32_t q = 0; q < m_qwords; q++)
    {
        uint64_t inverted = ~m_bitmap[q];
        if (inverted == 0) continue;

        unsigned long bitPos;
        if (_BitScanForward64(&bitPos, inverted))
        {
            uint32_t blockIndex = q * 64 + static_cast<uint32_t>(bitPos);
            if (blockIndex >= m_totalBlocks) return 0;

            m_bitmap[q] |= (1ULL << bitPos);
            return blockIndex + 1;  // 1-based，0 表示空
        }
    }
    return 0;
}

uint32_t BitmapAllocator::AllocateChain(uint32_t count)
{
    if (count == 0) return 0;

    uint32_t first = 0;
    uint32_t allocated = 0;

    for (uint32_t i = 0; i < m_totalBlocks && allocated < count; i++)
    {
        // 检查对应位的 qword 和 bit 位置
        uint32_t q = i / 64;
        uint32_t b = i % 64;

        if ((m_bitmap[q] & (1ULL << b)) == 0)
        {
            // 空闲
            m_bitmap[q] |= (1ULL << b);
            if (allocated == 0) first = i + 1;
            allocated++;
        }
    }

    return (allocated == count) ? first : 0;
}

void BitmapAllocator::Free(uint32_t blockIndex)
{
    if (blockIndex == 0 || blockIndex > m_totalBlocks) return;
    SetBit(blockIndex - 1, false);
}

void BitmapAllocator::FreeChain(uint32_t firstBlock)
{
    // 块链的释放由调用者遍历 nextBlock 逐个调用 Free
    // 这个方法只释放第一个块标记
    Free(firstBlock);
}

bool BitmapAllocator::IsFree(uint32_t blockIndex) const
{
    if (blockIndex == 0 || blockIndex > m_totalBlocks) return false;
    return !GetBit(blockIndex - 1);
}

uint32_t BitmapAllocator::UsedCount() const
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < m_totalBlocks; i++)
    {
        if (GetBit(i)) count++;
    }
    return count;
}

uint32_t BitmapAllocator::FreeCount() const
{
    return m_totalBlocks - UsedCount();
}

void BitmapAllocator::SetBit(uint32_t index, bool used)
{
    if (index >= m_totalBlocks) return;
    uint32_t q = index / 64;
    uint32_t b = index % 64;
    if (used)
        m_bitmap[q] |= (1ULL << b);
    else
        m_bitmap[q] &= ~(1ULL << b);
}

bool BitmapAllocator::GetBit(uint32_t index) const
{
    if (index >= m_totalBlocks) return false;
    uint32_t q = index / 64;
    uint32_t b = index % 64;
    return (m_bitmap[q] & (1ULL << b)) != 0;
}

} // namespace shmem
} // namespace pyczan
