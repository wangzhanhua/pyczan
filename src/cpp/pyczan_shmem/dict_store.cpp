#include "pyczan/shmem/dict_store.hpp"
#include <cstring>

namespace pyczan { namespace shmem {

DictStore::DictStore()
    : m_magic(nullptr), m_version(nullptr)
    , m_entryCount(nullptr), m_generation(nullptr)
    , m_cleanFlag(nullptr), m_crashed(nullptr)
    , m_allocOffset(0), m_blockBase(nullptr)
    , m_hashLocks(nullptr), m_hashOwners(nullptr)
{
}

// ─────────────────────────────────────────────────────────────
// Init — 在已映射的共享内存上布置所有指针
//
// 共享内存布局（VERSION=1）：
//   [magic(4) | version(4) | entryCount(4) | generation(4) |
//    cleanFlag(4) | crashed(4)]                                     24 字节
//   [hashLocks[256] (1024) | hashOwners[256] (1024)]               2048 字节
//   [hashTable[65521] (262084)]                                    262084 字节
//   [allocHeader (16)]                                                  16 字节
//   [blocks[N * 64]]
//
// initialize=true 时：
//   清零所有字段，写入 MAGIC/VERSION
//   分配器 Init 会建立初始空闲块（整个区域一块）
//   所有自旋锁清零
// ─────────────────────────────────────────────────────────────
void DictStore::Init(void* base, uint32_t totalSize, bool initialize)
{
    auto ptr = static_cast<char*>(base);

    // ── 头部字段（6 × 4 = 24 字节） ──
    m_magic      = (uint32_t*)ptr; ptr += 4;
    m_version    = (uint32_t*)ptr; ptr += 4;
    m_entryCount = (uint32_t*)ptr; ptr += 4;
    m_generation = (uint32_t*)ptr; ptr += 4;
    m_cleanFlag  = (uint32_t*)ptr; ptr += 4;
    m_crashed    = (uint32_t*)ptr; ptr += 4;

    // ── 哈希桶自旋锁池（256 × 8 = 2048 字节） ──
    m_hashLocks = (volatile LONG*)ptr; ptr += HASH_LOCK_POOL * 4;
    m_hashOwners = (DWORD*)ptr; ptr += HASH_LOCK_POOL * 4;

    // ── FNV-1a 哈希表（65521 × 4 = 262084 字节） ──
    m_hash.Init(ptr, HASH_SLOTS);
    ptr += HASH_SLOTS * sizeof(uint32_t);

    // ── FreeListAlloc（紧跟哈希表） ──
    m_allocOffset = (uint32_t)(ptr - static_cast<char*>(base));
    uint32_t headerEnd = m_allocOffset + FreeListAlloc::HEADER_SIZE;
    uint32_t blockBytes = totalSize > headerEnd ? totalSize - headerEnd : 0;
    m_alloc.Init(ptr, blockBytes / 64, initialize);

    // blockBase = 块数据区起始（= alloc 的 m_blocks）
    m_blockBase = static_cast<char*>(base) + m_allocOffset + FreeListAlloc::HEADER_SIZE;

    // ── 首次初始化：写头部 + 清零自旋锁 ──
    if (initialize)
    {
        *m_magic = MAGIC;
        *m_version = VERSION;
        *m_entryCount = 0;
        *m_generation = 0;
        *m_cleanFlag = 0;
        *m_crashed = 0;

        // allocLock/allocOwner 在 FreeListAlloc::Init 中不会被清零
        //（它只写 totalBlocks 和 freeHead），所以这里显式清零
        volatile LONG* alock = m_alloc.LockAddr();
        DWORD* aowner = m_alloc.OwnerAddr();
        if (alock) *alock = 0;
        if (aowner) *aowner = 0;

        // 哈希桶自旋锁清零
        for (uint32_t i = 0; i < HASH_LOCK_POOL; i++)
        {
            m_hashLocks[i] = 0;
            m_hashOwners[i] = 0;
        }
    }
}

bool DictStore::IsValid() const
{
    return m_magic && *m_magic == MAGIC
        && m_version && *m_version == VERSION;
}

bool DictStore::IsDirty() const
{
    // cleanFlag=0 且 generation>0 表示上次没有 clean 关闭
    return (m_cleanFlag && *m_cleanFlag == 0)
        && (m_generation && *m_generation > 0);
}

// ════════════════════════════════════════════════════════════════
// Hash 链表操作
// ════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────
// FindKey — 遍历 hash 链查找指定 key
//
// 每个 hash slot 维护一条单向链表（头插法）
// 链表节点就是 entry 本身，节点末尾 4 字节是 next 偏移
//
// 返回值：entry 起始地址（m_blockBase 加上 hash 表中存的偏移）
//         未找到返回 nullptr
//
// 注意：调用方需持有当前 slot 的哈希锁
// ─────────────────────────────────────────────────────────────
char* DictStore::FindKey(const std::string& key) const
{
    // 计算 key 的 hash 和 slot
    uint32_t h = m_hash.Hash(key.data(), (uint32_t)key.size());
    uint32_t slot = m_hash.Slot(h);

    // 遍历链表
    uint32_t bi = m_hash.Get(slot);
    while (bi)
    {
        char* p = m_blockBase + bi;

        // Entry 格式：type(1) | keyLen(4) | key | valLen(4) | value | nextHash(4)
        uint32_t kl = *(uint32_t*)(p + 1);
        if (kl == key.size() && memcmp(p + 5, key.data(), kl) == 0)
            return p;  // 找到了

        // 跳到下一个节点（最后 4 字节 = next 偏移）
        bi = *(uint32_t*)(p + 9 + kl + *(uint32_t*)(p + 5 + kl));
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────
// RemoveKey — 从 hash 链中移除指定 key 的 entry
//
// 遍历链表同时维护 prev 指针，找到后修改 prev 的 next 指向 cur 的 next
// 不释放 entry 的内存（由调用方通过 FreeListAlloc::Free 处理）
//
// 注意：调用方需持有当前 slot 的哈希锁
// ─────────────────────────────────────────────────────────────
void DictStore::RemoveKey(const std::string& key)
{
    uint32_t h = m_hash.Hash(key.data(), (uint32_t)key.size());
    uint32_t slot = m_hash.Slot(h);

    uint32_t prev = 0;       // 前一个节点的 block offset
    uint32_t cur = m_hash.Get(slot);  // 当前遍历到的节点

    while (cur)
    {
        char* p = m_blockBase + cur;
        uint32_t kl = *(uint32_t*)(p + 1);
        if (kl == key.size() && memcmp(p + 5, key.data(), kl) == 0)
        {
            // 找到目标节点，它的 next 位置 = 9 + kl + vl
            uint32_t nh = *(uint32_t*)(p + 9 + kl + *(uint32_t*)(p + 5 + kl));

            if (prev == 0)
                m_hash.Set(slot, nh);          // 目标在头部
            else {
                // 修改前一个节点的 next
                char* pp = m_blockBase + prev;
                uint32_t pk = *(uint32_t*)(pp + 1);
                *(uint32_t*)(pp + 9 + pk + *(uint32_t*)(pp + 5 + pk)) = nh;
            }
            return;
        }
        prev = cur;
        uint32_t kk = *(uint32_t*)(p + 1);
        cur = *(uint32_t*)(p + 9 + kk + *(uint32_t*)(p + 5 + kk));
    }
}

// ── 计算 key 对应的 slot ──
uint32_t DictStore::SlotOf(const std::string& key) const
{
    uint32_t h = m_hash.Hash(key.data(), (uint32_t)key.size());
    return m_hash.Slot(h);
}

// ─────────────────────────────────────────────────────────────
// LinkEntry — 将新 entry 链入 hash 表（头插法）
//
// 把新 entry 插入到 slot 链表的头部：
//   新 entry->next = 当前头部
//   设置 slot = 新 entry 的 block offset
//
// blockOff 是 entry 相对于 m_blockBase 的偏移
// 由 InsertEntry 后 Alloc 返回的指针计算得到
// ─────────────────────────────────────────────────────────────
void DictStore::LinkEntry(uint32_t slot, uint32_t blockOff,
                          uint32_t keyLen, uint32_t valLen)
{
    // Entry 最后 4 字节是 next 指针
    char* p = m_blockBase + blockOff;
    *(uint32_t*)(p + 9 + keyLen + valLen) = m_hash.Get(slot);
    m_hash.Set(slot, blockOff);
}

// ─────────────────────────────────────────────────────────────
// CollectKeys — 遍历所有 hash slot 收集 key
//
// 注意：调用方需在遍历期间持有所有 256 个哈希锁
// 否则可能在遍历过程中插入/删除 key，导致结果不一致
// ─────────────────────────────────────────────────────────────
void DictStore::CollectKeys(std::vector<std::string>& out) const
{
    for (uint32_t s = 0; s < HASH_SLOTS; s++)
    {
        uint32_t bi = m_hash.Get(s);
        while (bi)
        {
            char* p = m_blockBase + bi;
            uint32_t kl = *(uint32_t*)(p + 1);
            out.emplace_back(p + 5, kl);
            bi = *(uint32_t*)(p + 9 + kl + *(uint32_t*)(p + 5 + kl));
        }
    }
}

// ── Clear：清零哈希表 + 重置 entry count ──
void DictStore::Clear()
{
    m_hash.Clear();
    *m_entryCount = 0;
}

}} // namespace pyczan::shmem
