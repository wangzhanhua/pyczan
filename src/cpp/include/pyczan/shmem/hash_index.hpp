#ifndef PYCZAN_SHMEM_HASH_INDEX_H_
#define PYCZAN_SHMEM_HASH_INDEX_H_

#include <cstdint>

namespace pyczan {
namespace shmem {

// 哈希索引：key → 首块编号
// 存储在共享内存中，冲突用块内的 nextHash 指针链式解决
class HashIndex
{
public:
    HashIndex();

    // 绑定到共享内存中的哈希表区域
    void Init(void* tableBase, uint32_t slots);

    // FNV-1a 哈希
    uint32_t Hash(const char* data, uint32_t len) const;

    // 计算槽位
    uint32_t Slot(uint32_t hashValue) const;

    // 获取槽位的首块编号（0 = 空）
    uint32_t Get(uint32_t slot) const;

    // 设置槽位的首块编号
    void Set(uint32_t slot, uint32_t blockIndex);

    // 清空所有槽
    void Clear();

    uint32_t Slots() const { return m_slots; }

private:
    uint32_t* m_table;
    uint32_t  m_slots;
};

} // namespace shmem
} // namespace pyczan

#endif // PYCZAN_SHMEM_HASH_INDEX_H_
