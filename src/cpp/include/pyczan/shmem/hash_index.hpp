#ifndef PYCZAN_SHMEM_HASH_INDEX_H_
#define PYCZAN_SHMEM_HASH_INDEX_H_

#include <cstdint>
#include <cstring>

namespace pyczan { namespace shmem {

// ─────────────────────────────────────────────────────────────
// HashIndex — FNV-1a 哈希表
//
// 作用：将字符串 key 映射到一个 32 位 slot 索引
//       每个 slot 存一个 32 位值（在 Dict 中用作链表的头指针）
//
// 哈希函数：FNV-1a（Fowler-Noll-Vo）
//   比 FNV-1 更好的雪崩特性，32 位版本广泛用于哈希表
//   初始值 2166136261，质数 16777619
//
// 内存位置：
//   这个类的数据指针指向共享内存中的一段连续区域
//   多个进程映射同一共享内存后，访问同一份数据
//
// 线程安全：
//   本类不做同步，需要外层加锁
//   当前由 Dict::_HashLock(slot) 保护
// ─────────────────────────────────────────────────────────────
class HashIndex
{
public:
    HashIndex() : m_table(nullptr), m_slots(0) {}

    // 设置哈希表在共享内存中的位置
    // tableBase: 共享内存中的 slot 数组基址
    // slots: slot 数量（Dict 中固定 65521）
    void Init(void* tableBase, uint32_t slots)
    {
        m_table = static_cast<uint32_t*>(tableBase);
        m_slots = slots;
    }

    // FNV-1a 哈希：将任意字节序列映射为 32 位哈希值
    uint32_t Hash(const char* data, uint32_t len) const
    {
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < len; i++)
        {
            h ^= static_cast<uint8_t>(data[i]);
            h *= 16777619u;
        }
        return h;
    }

    // 哈希值 → slot 索引（取模）
    uint32_t Slot(uint32_t hashValue) const
    {
        return hashValue % m_slots;
    }

    // 读取 / 写入 slot（共享内存直接访问）
    uint32_t Get(uint32_t slot) const
    {
        return (slot < m_slots) ? m_table[slot] : 0;
    }
    void Set(uint32_t slot, uint32_t v)
    {
        if (slot < m_slots) m_table[slot] = v;
    }

    // 清零所有 slot
    void Clear()
    {
        if (m_table)
            std::memset(m_table, 0, m_slots * sizeof(uint32_t));
    }

    uint32_t Slots() const { return m_slots; }

private:
    uint32_t* m_table;   // 指向共享内存中的 slot 数组
    uint32_t  m_slots;   // slot 数量
};

}} // namespace pyczan::shmem

#endif
