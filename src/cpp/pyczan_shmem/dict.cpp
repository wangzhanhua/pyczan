#include "pyczan/shmem/dict.hpp"
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace pyczan {
namespace shmem {

Dict::Dict()
    : m_hMutex(nullptr), m_hEvent(nullptr), m_hMapping(nullptr)
    , m_pView(nullptr), m_totalSize(0)
    , m_entryCount(nullptr), m_generation(nullptr), m_cleanFlag(nullptr)
    , m_allocOffset(0), m_lockContention(0)
{
}

Dict::~Dict() { Close(); }

// ============================================================
// OpenOrCreate / Close
// ============================================================

bool Dict::OpenOrCreate(const std::string& name, uint32_t totalSize)
{
    if (m_hMutex) return true;
    m_name = name;
    m_totalSize = totalSize;

    m_hMutex = CreateMutexA(NULL, FALSE, ("pyczan_shmem_mtx_" + name).c_str());
    if (!m_hMutex) throw std::runtime_error("CreateMutex failed");
    m_hEvent = CreateEventA(NULL, FALSE, FALSE, ("pyczan_shmem_evt_" + name).c_str());

    m_hMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, totalSize,
                                     ("pyczan_shmem_" + name).c_str());
    if (!m_hMapping) throw std::runtime_error("CreateFileMapping failed");
    bool isNew = (GetLastError() != ERROR_ALREADY_EXISTS);

    m_pView = MapViewOfFile(m_hMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!m_pView) throw std::runtime_error("MapViewOfFile failed");

    // 布局: [entryCount(4) | generation(4) | cleanFlag(4) | hash(HASH_SLOTS*4) | allocHeader(8) | blocks]
    auto ptr = static_cast<char*>(m_pView);
    m_entryCount = (uint32_t*)ptr; ptr += 4;
    m_generation  = (uint32_t*)ptr; ptr += 4;
    m_cleanFlag   = (uint32_t*)ptr; ptr += 4;
    m_hash.Init(ptr, HASH_SLOTS);
    ptr += HASH_SLOTS * sizeof(uint32_t);
    m_allocOffset = (uint32_t)(ptr - static_cast<char*>(m_pView));

    uint32_t headerEnd = m_allocOffset + 8;
    uint32_t blockBytes = totalSize > headerEnd ? totalSize - headerEnd : 0;
    m_alloc.Init(static_cast<char*>(m_pView) + m_allocOffset, blockBytes / 64, isNew);

    if (isNew) {
        *m_entryCount = 0; *m_generation = 0; *m_cleanFlag = 0;
    } else if (*m_cleanFlag == 0 && *m_generation > 0) {
        // 崩溃检测：cleanFlag=0 且 generation>0 → 上次未正常关闭
        m_hash.Clear();
        uint32_t hEnd = m_allocOffset + 8;
        m_alloc.Init(static_cast<char*>(m_pView) + m_allocOffset,
                     (totalSize > hEnd ? totalSize - hEnd : 0) / 64, true);
        *m_entryCount = 0; *m_generation = 0; *m_cleanFlag = 1;
    }

    m_lockContention = 0;
    return true;
}

void Dict::Close()
{
    m_name.clear();
    if (m_cleanFlag) *m_cleanFlag = 1;
    if (m_hEvent) { CloseHandle(m_hEvent); m_hEvent = nullptr; }
    if (m_hMutex) { CloseHandle(m_hMutex); m_hMutex = nullptr; }
    if (m_pView) { UnmapViewOfFile(m_pView); m_pView = nullptr; }
    if (m_hMapping) { CloseHandle(m_hMapping); m_hMapping = nullptr; }
    m_cleanFlag = nullptr; m_generation = nullptr; m_entryCount = nullptr;
}

void Dict::Reset(const std::string& name)
{
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536,
                                      ("pyczan_shmem_" + name).c_str());
    if (!hMap) return;
    bool exists = (GetLastError() == ERROR_ALREADY_EXISTS);
    if (!exists) { CloseHandle(hMap); return; }
    LPVOID view = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!view) { CloseHandle(hMap); return; }
    auto ec = static_cast<uint32_t*>(view);
    ec[0] = 0; ec[1] = 0; ec[2] = 0;
    UnmapViewOfFile(view); CloseHandle(hMap);

    HANDLE mtx = CreateMutexA(NULL, TRUE, ("pyczan_shmem_mtx_" + name).c_str());
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
}

// ============================================================
// 锁
// ============================================================

bool Dict::Lock(uint32_t timeout)
{
    if (!m_hMutex) return false;
    DWORD r = WaitForSingleObject(m_hMutex, timeout);
    if (r == WAIT_ABANDONED) { m_lockContention++; return true; }
    return r == WAIT_OBJECT_0;
}

void Dict::Unlock() { if (m_hMutex) ReleaseMutex(m_hMutex); }

// ============================================================
// Alloc / Free
// ============================================================

void* Dict::Alloc(uint32_t bytes) { return m_alloc.Alloc(bytes); }
void Dict::Free(void* ptr) { m_alloc.Free(ptr); }

// ============================================================
// Hash helpers
// ============================================================

static char* FindKey(HashIndex& hash, const std::string& key, char* base)
{
    uint32_t h = hash.Hash(key.data(), (uint32_t)key.size());
    uint32_t slot = hash.Slot(h);
    uint32_t bi = hash.Get(slot);
    while (bi) {
        char* p = base + bi;
        uint32_t kl = *(uint32_t*)(p + 1);
        if (kl == key.size() && memcmp(p + 5, key.data(), kl) == 0) return p;
        bi = *(uint32_t*)(p + 9 + kl + *(uint32_t*)(p + 5 + kl));
    }
    return nullptr;
}

static void RemoveKey(HashIndex& hash, const std::string& key, char* base)
{
    uint32_t h = hash.Hash(key.data(), (uint32_t)key.size());
    uint32_t slot = hash.Slot(h);
    uint32_t prev = 0, cur = hash.Get(slot);
    while (cur) {
        char* p = base + cur;
        uint32_t kl = *(uint32_t*)(p + 1);
        if (kl == key.size() && memcmp(p + 5, key.data(), kl) == 0) {
            uint32_t nh = *(uint32_t*)(p + 9 + kl + *(uint32_t*)(p + 5 + kl));
            if (prev == 0) hash.Set(slot, nh);
            else { char* pp = base + prev; uint32_t pk = *(uint32_t*)(pp + 1);
                *(uint32_t*)(pp + 9 + pk + *(uint32_t*)(pp + 5 + pk)) = nh; }
            return;
        }
        prev = cur; uint32_t kk = *(uint32_t*)(p + 1);
        cur = *(uint32_t*)(p + 9 + kk + *(uint32_t*)(p + 5 + kk));
    }
}

// ============================================================
// Set / Get / Delete / Has / Size / Clear / Keys / Status
// ============================================================

void Dict::Set(const std::string& key, const std::string& value, uint8_t type)
{
    if (!m_hMutex) throw std::runtime_error("Dict not open");
    if (!Lock()) throw std::runtime_error("Lock timeout");
    try {
        char* base = static_cast<char*>(m_pView) + m_allocOffset + 8;
        char* old = FindKey(m_hash, key, base);
        if (old) { RemoveKey(m_hash, key, base); m_alloc.Free(old); (*m_entryCount)--; }

        uint32_t kl = (uint32_t)key.size();
        uint32_t vl = (uint32_t)value.size();
        uint32_t esize = 9 + kl + vl + 4;
        void* ptr = m_alloc.Alloc(esize);
        if (!ptr) throw std::runtime_error("Out of shared memory");
        auto buf = static_cast<char*>(ptr);
        buf[0] = (char)type;
        memcpy(buf + 1, &kl, 4); memcpy(buf + 5, key.data(), kl);
        memcpy(buf + 5 + kl, &vl, 4); memcpy(buf + 9 + kl, value.data(), vl);

        uint32_t h = m_hash.Hash(key.data(), kl);
        uint32_t slot = m_hash.Slot(h);
        *(uint32_t*)(buf + 9 + kl + vl) = m_hash.Get(slot);
        m_hash.Set(slot, (uint32_t)((char*)ptr - base));
        (*m_entryCount)++; (*m_generation)++;
        if (m_hEvent) SetEvent(m_hEvent);
        Unlock();
    } catch (...) { Unlock(); throw; }
}

std::string Dict::Get(const std::string& key, uint8_t* outType)
{
    if (!m_hMutex) throw std::runtime_error("Dict not open");
    if (!Lock()) throw std::runtime_error("Lock timeout");
    try {
        char* base = static_cast<char*>(m_pView) + m_allocOffset + 8;
        char* ptr = FindKey(m_hash, key, base);
        if (!ptr) { Unlock(); throw std::out_of_range("Key not found: " + key); }
        if (outType) *outType = (uint8_t)ptr[0];
        uint32_t kl = *(uint32_t*)(ptr + 1);
        uint32_t vl = *(uint32_t*)(ptr + 5 + kl);
        std::string r((const char*)(ptr + 9 + kl), vl);
        Unlock(); return r;
    } catch (const std::out_of_range&) { Unlock(); throw; }
    catch (...) { Unlock(); throw; }
}

bool Dict::Delete(const std::string& key)
{
    if (!m_hMutex) throw std::runtime_error("Dict not open");
    if (!Lock()) throw std::runtime_error("Lock timeout");
    try {
        char* base = static_cast<char*>(m_pView) + m_allocOffset + 8;
        char* ptr = FindKey(m_hash, key, base);
        if (!ptr) { Unlock(); return false; }
        RemoveKey(m_hash, key, base);
        m_alloc.Free(ptr);
        (*m_entryCount)--; (*m_generation)++;
        Unlock(); return true;
    } catch (...) { Unlock(); throw; }
}

bool Dict::Has(const std::string& key)
{
    if (!m_hMutex) return false;
    if (!Lock()) return false;
    char* base = static_cast<char*>(m_pView) + m_allocOffset + 8;
    bool r = FindKey(m_hash, key, base) != nullptr;
    Unlock(); return r;
}

uint32_t Dict::Size() { return m_entryCount ? *m_entryCount : 0; }

void Dict::Clear()
{
    if (!m_hMutex) return;
    if (!Lock()) throw std::runtime_error("Lock timeout");
    m_hash.Clear();
    *m_entryCount = 0;
    uint32_t hEnd = m_allocOffset + 8;
    m_alloc.Init(static_cast<char*>(m_pView) + m_allocOffset,
                 (m_totalSize > hEnd ? m_totalSize - hEnd : 0) / 64, true);
    Unlock();
}

std::vector<std::string> Dict::Keys()
{
    std::vector<std::string> keys;
    if (!m_hMutex) return keys;
    if (!Lock()) return keys;
    char* base = static_cast<char*>(m_pView) + m_allocOffset + 8;
    for (uint32_t s = 0; s < HASH_SLOTS; s++) {
        uint32_t bi = m_hash.Get(s);
        while (bi) {
            char* p = base + bi;
            uint32_t kl = *(uint32_t*)(p + 1);
            keys.emplace_back(p + 5, kl);
            bi = *(uint32_t*)(p + 9 + kl + *(uint32_t*)(p + 5 + kl));
        }
    }
    Unlock(); return keys;
}

Dict::StatusInfo Dict::Status()
{
    StatusInfo info = {};
    info.entries = Size();
    info.totalBlocks = m_alloc.TotalBlocks();
    info.usedBlocks = m_alloc.UsedBlocks();
    info.freeFragments = m_alloc.FreeFragments();
    info.lockContention = m_lockContention;
    info.generation = m_generation ? *m_generation : 0;
    info.wasCrashed = (m_cleanFlag && *m_cleanFlag == 0 && m_generation && *m_generation > 0);
    return info;
}

} // namespace shmem
} // namespace pyczan
