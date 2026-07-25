#include "pyczan/shmem/block_zone.hpp"
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace pyczan {
namespace shmem {

BlockZone::BlockZone()
    : m_hMapping(nullptr), m_pView(nullptr), m_blocks(nullptr)
    , m_blockSize(0), m_blockCount(0)
{
}

BlockZone::~BlockZone() { Close(); }

void BlockZone::Close()
{
    if (m_pView) { UnmapViewOfFile(m_pView); m_pView = nullptr; }
    if (m_hMapping) { CloseHandle(m_hMapping); m_hMapping = nullptr; }
    m_blocks = nullptr;
}

// 布局: [entryCount(4) | reserved(4) | hash(HASH_SLOTS*4) | bitmap(qwords*8) | blocks]
uint32_t BlockZone::HashBytes() const { return HASH_SLOTS * sizeof(uint32_t); }
uint32_t BlockZone::BitmapBytes() const { return ((m_blockCount + 63) / 64) * sizeof(uint64_t); }
uint32_t BlockZone::HeaderBytes() const { return 8 + HashBytes() + BitmapBytes(); }
uint32_t* BlockZone::EntryCountPtr() const { return static_cast<uint32_t*>(m_pView); }

void BlockZone::Open(const std::string& name, uint32_t blockSize, uint32_t totalSize, bool& isNew)
{
    m_blockSize = blockSize;

    // 计算块数
    uint32_t hashBytes = HASH_SLOTS * sizeof(uint32_t);
    uint32_t qwords = (totalSize / blockSize + 63) / 64;
    uint32_t bitmapBytes = qwords * sizeof(uint64_t);
    uint32_t headerBytes = 8 + hashBytes + bitmapBytes;
    m_blockCount = (totalSize - 8 - hashBytes - bitmapBytes) / blockSize;

    // 超过可用空间则调整
    uint32_t mapSize = 8 + hashBytes + bitmapBytes + m_blockCount * blockSize;

    std::string mapName = "pyczan_shmem_" + name;
    m_hMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, mapSize, mapName.c_str());
    if (!m_hMapping) throw std::runtime_error("CreateFileMapping failed");
    isNew = (GetLastError() != ERROR_ALREADY_EXISTS);

    m_pView = MapViewOfFile(m_hMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!m_pView) throw std::runtime_error("MapViewOfFile failed");

    // 定位各组件
    auto ptr = static_cast<char*>(m_pView);
    m_hash.Init(ptr + 8, HASH_SLOTS);  // hash starts after entryCount(4) + reserved(4)
    if (isNew) {
        *EntryCountPtr() = 0;
        m_hash.Clear();
    }
    m_bitmap.Init(ptr + 8 + hashBytes, m_blockCount);
    m_blocks = ptr + 8 + hashBytes + bitmapBytes;
    if (isNew) memset(m_blocks, 0, m_blockCount * m_blockSize);
}

uint32_t BlockZone::EntryCount() const
{
    return m_pView ? *EntryCountPtr() : 0;
}

void BlockZone::SetEntryCount(uint32_t count)
{
    if (m_pView) *EntryCountPtr() = count;
}

void* BlockZone::BlockAddr(uint32_t idx) const
{
    return static_cast<char*>(m_blocks) + (idx - 1) * m_blockSize;
}

void BlockZone::SetNext(uint32_t idx, uint32_t next)
{
    *reinterpret_cast<uint32_t*>(static_cast<char*>(BlockAddr(idx)) + m_blockSize - 8) = next;
}

uint32_t BlockZone::ReadNext(uint32_t idx) const
{
    return *reinterpret_cast<uint32_t*>(static_cast<char*>(BlockAddr(idx)) + m_blockSize - 8);
}

void BlockZone::SetHash(uint32_t idx, uint32_t nextHash)
{
    *reinterpret_cast<uint32_t*>(static_cast<char*>(BlockAddr(idx)) + m_blockSize - 4) = nextHash;
}

uint32_t BlockZone::ReadHash(uint32_t idx) const
{
    return *reinterpret_cast<uint32_t*>(static_cast<char*>(BlockAddr(idx)) + m_blockSize - 4);
}

// ============================================================
// 写入
// ============================================================

void BlockZone::WriteHeader(uint32_t idx, const std::string& key, const std::string& value,
                            uint32_t valOff, uint32_t valLen, uint32_t nextBlock)
{
    auto addr = static_cast<char*>(BlockAddr(idx));
    uint32_t kl = (uint32_t)key.size();
    uint32_t vl = (uint32_t)value.size();
    addr[0] = 0;  // flags
    memcpy(addr + 1, &kl, 4);
    memcpy(addr + 5, key.data(), kl);
    memcpy(addr + 5 + kl, &vl, 4);
    memcpy(addr + 9 + kl, value.data() + valOff, valLen);
    *reinterpret_cast<uint32_t*>(addr + m_blockSize - 8) = nextBlock;
    *reinterpret_cast<uint32_t*>(addr + m_blockSize - 4) = 0;
}

void BlockZone::WriteData(uint32_t idx, const char* data, uint32_t len, uint32_t nextBlock)
{
    auto addr = static_cast<char*>(BlockAddr(idx));
    uint32_t cp = len < Payload(m_blockSize) ? len : Payload(m_blockSize);
    memcpy(addr, data, cp);
    *reinterpret_cast<uint32_t*>(addr + m_blockSize - 8) = nextBlock;
    *reinterpret_cast<uint32_t*>(addr + m_blockSize - 4) = 0;
}

// ============================================================
// 哈希操作
// ============================================================

void BlockZone::InsertHash(uint32_t slot, uint32_t idx)
{
    uint32_t head = m_hash.Get(slot);
    SetHash(idx, head);
    m_hash.Set(slot, idx);
}

void BlockZone::RemoveHash(uint32_t slot, uint32_t idx)
{
    uint32_t prev = 0, cur = m_hash.Get(slot);
    while (cur) {
        if (cur == idx) {
            uint32_t nh = ReadHash(cur);
            if (prev == 0) m_hash.Set(slot, nh);
            else SetHash(prev, nh);
            return;
        }
        prev = cur;
        cur = ReadHash(cur);
    }
}

// ============================================================
// FindKey
// ============================================================

uint32_t BlockZone::FindKey(const std::string& key) const
{
    uint32_t h = m_hash.Hash(key.data(), (uint32_t)key.size());
    uint32_t slot = m_hash.Slot(h);
    uint32_t b = m_hash.Get(slot);
    while (b) {
        void* addr = BlockAddr(b);
        uint32_t kl = *reinterpret_cast<uint32_t*>(static_cast<char*>(addr) + 1);
        if (kl == key.size() && memcmp(static_cast<char*>(addr) + 5, key.data(), kl) == 0)
            return b;
        b = *reinterpret_cast<uint32_t*>(static_cast<char*>(addr) + m_blockSize - 4);
    }
    return 0;
}

// ============================================================
// Set
// ============================================================

void BlockZone::Set(const std::string& key, const std::string& value, bool notify)
{
    (void)notify;
    uint32_t kl = (uint32_t)key.size();
    uint32_t vl = (uint32_t)value.size();
    uint32_t overhead = 9 + kl;
    uint32_t pay = Payload(m_blockSize);

    uint32_t h = m_hash.Hash(key.data(), kl);
    uint32_t slot = m_hash.Slot(h);

    // 删除旧的
    uint32_t old = FindKey(key);
    if (old) {
        uint32_t cur = old;
        while (cur) {
            uint32_t nxt = ReadNext(cur);
            m_bitmap.Free(cur);
            cur = nxt;
        }
        RemoveHash(slot, old);
        *EntryCountPtr() = EntryCount() - 1;
    }

    // 计算块数
    uint32_t total = overhead + vl;
    uint32_t blocks = 1;
    if (total > pay) blocks += (total - pay + pay - 1) / pay;

    // 逐个分配并链式写入
    uint32_t first = 0, prev = 0;
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t cur = m_bitmap.Allocate();
        if (!cur) throw std::runtime_error("Out of memory");
        if (i == 0) first = cur;
        else SetNext(prev, cur);  // 链接上一个块 → 当前块

        if (i == 0) {
            // 头块：先写 nextBlock=0，后续若有两块再更新
            uint32_t firstVal = pay - overhead;
            uint32_t wv = vl < firstVal ? vl : firstVal;
            WriteHeader(cur, key, value, 0, wv, 0);
        } else {
            uint32_t already = (pay - overhead) + (i - 1) * pay;
            uint32_t wv = (vl - already) < pay ? (vl - already) : pay;
            WriteData(cur, value.data() + already, wv, 0);
        }
        prev = cur;
    }

    InsertHash(slot, first);
    *EntryCountPtr() = EntryCount() + 1;
}

// ============================================================
// Get
// ============================================================

std::string BlockZone::Get(const std::string& key)
{
    uint32_t b = FindKey(key);
    if (!b) throw std::out_of_range("Key not found: " + key);

    uint32_t pay = Payload(m_blockSize);
    void* addr = BlockAddr(b);
    uint32_t kl = *reinterpret_cast<uint32_t*>(static_cast<char*>(addr) + 1);
    uint32_t vl = *reinterpret_cast<uint32_t*>(static_cast<char*>(addr) + 5 + kl);
    uint32_t overhead = 9 + kl;
    uint32_t firstVal = pay - overhead;

    std::string result;
    result.resize(vl);
    uint32_t rf = vl < firstVal ? vl : firstVal;
    memcpy(&result[0], static_cast<char*>(addr) + 9 + kl, rf);
    uint32_t off = rf;

    uint32_t nb = ReadNext(b);
    while (nb && off < vl) {
        void* na = BlockAddr(nb);
        uint32_t rv = (vl - off) < pay ? (vl - off) : pay;
        memcpy(&result[off], na, rv);
        off += rv;
        nb = ReadNext(nb);
    }
    return result;
}

// ============================================================
// Delete / Has / Clear / Keys
// ============================================================

bool BlockZone::Delete(const std::string& key, bool notify)
{
    (void)notify;
    uint32_t b = FindKey(key);
    if (!b) return false;

    uint32_t h = m_hash.Hash(key.data(), (uint32_t)key.size());
    uint32_t slot = m_hash.Slot(h);

    uint32_t cur = b;
    while (cur) {
        uint32_t nxt = ReadNext(cur);
        m_bitmap.Free(cur);
        cur = nxt;
    }
    RemoveHash(slot, b);
    *EntryCountPtr() = EntryCount() - 1;
    return true;
}

bool BlockZone::Has(const std::string& key)
{
    return FindKey(key) != 0;
}

void BlockZone::Clear()
{
    m_hash.Clear();
    uint32_t qw = (m_blockCount + 63) / 64;
    memset(m_blocks, 0, qw * sizeof(uint64_t));
    *EntryCountPtr() = 0;
}

std::vector<std::string> BlockZone::Keys()
{
    std::vector<std::string> keys;
    for (uint32_t s = 0; s < HASH_SLOTS; s++) {
        uint32_t b = m_hash.Get(s);
        while (b) {
            void* addr = BlockAddr(b);
            uint32_t kl = *reinterpret_cast<uint32_t*>(static_cast<char*>(addr) + 1);
            keys.emplace_back(static_cast<char*>(addr) + 5, kl);
            b = *reinterpret_cast<uint32_t*>(static_cast<char*>(addr) + m_blockSize - 4);
        }
    }
    return keys;
}

} // namespace shmem
} // namespace pyczan
