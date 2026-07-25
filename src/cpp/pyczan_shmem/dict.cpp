#include "pyczan/shmem/dict.hpp"
#include <stdexcept>

namespace pyczan {
namespace shmem {

const float Dict::SMALL_RATIO = 0.1f;

Dict::Dict() : m_hMutex(nullptr), m_hEvent(nullptr) {}
Dict::~Dict() { Close(); }

bool Dict::OpenOrCreate(const std::string& name, uint32_t totalSize)
{
    if (m_hMutex) return true;
    m_name = name;

    m_hMutex = CreateMutexA(NULL, FALSE, ("pyczan_shmem_mtx_" + name).c_str());
    if (!m_hMutex) throw std::runtime_error("CreateMutex failed");
    m_hEvent = CreateEventA(NULL, FALSE, FALSE, ("pyczan_shmem_evt_" + name).c_str());

    uint32_t smallTotal = (uint32_t)(totalSize * SMALL_RATIO);
    smallTotal = (smallTotal / BLOCK_SMALL) * BLOCK_SMALL;
    if (smallTotal < BLOCK_SMALL * 64) smallTotal = BLOCK_SMALL * 64;

    bool isNew;
    m_small.Open(name + "_s", BLOCK_SMALL, smallTotal, isNew);
    m_large.Open(name + "_l", BLOCK_LARGE, totalSize - smallTotal, isNew);
    return true;
}

void Dict::Close()
{
    m_name.clear();
    if (m_hEvent) { CloseHandle(m_hEvent); m_hEvent = nullptr; }
    if (m_hMutex) { CloseHandle(m_hMutex); m_hMutex = nullptr; }
    m_small.Close();
    m_large.Close();
}

bool Dict::Lock(uint32_t timeout)
{
    if (!m_hMutex) return false;
    DWORD r = WaitForSingleObject(m_hMutex, timeout);
    return (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
}
void Dict::Unlock() { if (m_hMutex) ReleaseMutex(m_hMutex); }

BlockZone& Dict::PickZone(const std::string& key, const std::string& value)
{
    uint32_t overhead = 9 + (uint32_t)key.size();
    return (overhead + (uint32_t)value.size() <= 56) ? m_small : m_large;
}

void Dict::Set(const std::string& key, const std::string& value)
{
    if (!m_hMutex) throw std::runtime_error("Dict not open");
    if (!Lock()) throw std::runtime_error("Lock timeout");
    try {
        m_small.Delete(key, false);
        m_large.Delete(key, false);
        BlockZone& zone = PickZone(key, value);
        zone.Set(key, value, false);
        if (m_hEvent) SetEvent(m_hEvent);
        Unlock();
    } catch (...) { Unlock(); throw; }
}

std::string Dict::Get(const std::string& key)
{
    if (!m_hMutex) throw std::runtime_error("Dict not open");
    if (!Lock()) throw std::runtime_error("Lock timeout");
    try {
        if (m_small.Has(key)) { std::string v = m_small.Get(key); Unlock(); return v; }
        std::string v = m_large.Get(key); Unlock(); return v;
    } catch (const std::out_of_range&) { Unlock(); throw; }
    catch (...) { Unlock(); throw; }
}

bool Dict::Delete(const std::string& key)
{
    if (!m_hMutex) throw std::runtime_error("Dict not open");
    if (!Lock()) throw std::runtime_error("Lock timeout");
    try {
        bool r = m_small.Delete(key, false) || m_large.Delete(key, false);
        Unlock(); return r;
    } catch (...) { Unlock(); throw; }
}

bool Dict::Has(const std::string& key)
{
    if (!m_hMutex) return false;
    if (!Lock()) return false;
    bool r = m_small.Has(key) || m_large.Has(key);
    Unlock(); return r;
}

uint32_t Dict::Size() { return m_small.EntryCount() + m_large.EntryCount(); }

void Dict::Clear()
{
    if (!m_hMutex) return;
    if (!Lock()) throw std::runtime_error("Lock timeout");
    m_small.Clear();
    m_large.Clear();
    Unlock();
}

std::vector<std::string> Dict::Keys()
{
    std::vector<std::string> keys;
    if (!m_hMutex) return keys;
    if (!Lock()) return keys;
    auto ks = m_small.Keys();
    keys.insert(keys.end(), ks.begin(), ks.end());
    ks = m_large.Keys();
    keys.insert(keys.end(), ks.begin(), ks.end());
    Unlock();
    return keys;
}

} // namespace shmem
} // namespace pyczan
