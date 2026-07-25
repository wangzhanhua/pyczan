#ifndef PYCZAN_SHMEM_HASH_INDEX_H_
#define PYCZAN_SHMEM_HASH_INDEX_H_

#include <cstdint>
#include <cstring>

namespace pyczan {
namespace shmem {

class HashIndex
{
public:
    HashIndex() : m_table(nullptr), m_slots(0) {}

    void Init(void* tableBase, uint32_t slots) {
        m_table = static_cast<uint32_t*>(tableBase);
        m_slots = slots;
    }

    uint32_t Hash(const char* data, uint32_t len) const {
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < len; i++)
        { h ^= static_cast<uint8_t>(data[i]); h *= 16777619u; }
        return h;
    }

    uint32_t Slot(uint32_t hashValue) const { return hashValue % m_slots; }
    uint32_t Get(uint32_t slot) const { return (slot < m_slots) ? m_table[slot] : 0; }
    void Set(uint32_t slot, uint32_t v) { if (slot < m_slots) m_table[slot] = v; }

    void Clear() { if (m_table) std::memset(m_table, 0, m_slots * sizeof(uint32_t)); }
    uint32_t Slots() const { return m_slots; }

private:
    uint32_t* m_table;
    uint32_t  m_slots;
};

} // namespace shmem
} // namespace pyczan

#endif
