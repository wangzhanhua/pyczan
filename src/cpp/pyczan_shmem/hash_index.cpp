#include "pyczan/shmem/hash_index.hpp"
#include <cstring>

namespace pyczan {
namespace shmem {

HashIndex::HashIndex()
    : m_table(nullptr)
    , m_slots(0)
{
}

void HashIndex::Init(void* tableBase, uint32_t slots)
{
    m_table = static_cast<uint32_t*>(tableBase);
    m_slots = slots;
}

uint32_t HashIndex::Hash(const char* data, uint32_t len) const
{
    // FNV-1a
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < len; i++)
    {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 16777619u;
    }
    return hash;
}

uint32_t HashIndex::Slot(uint32_t hashValue) const
{
    return hashValue % m_slots;
}

uint32_t HashIndex::Get(uint32_t slot) const
{
    if (slot >= m_slots) return 0;
    return m_table[slot];
}

void HashIndex::Set(uint32_t slot, uint32_t blockIndex)
{
    if (slot >= m_slots) return;
    m_table[slot] = blockIndex;
}

void HashIndex::Clear()
{
    if (m_table)
    {
        std::memset(m_table, 0, m_slots * sizeof(uint32_t));
    }
}

} // namespace shmem
} // namespace pyczan
