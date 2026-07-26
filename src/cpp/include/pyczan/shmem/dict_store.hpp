#ifndef PYCZAN_SHMEM_DICT_STORE_H_
#define PYCZAN_SHMEM_DICT_STORE_H_

#include "freelist.hpp"
#include "hash_index.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace pyczan { namespace shmem {

// ─────────────────────────────────────────────────────────────
// DictStore — 共享内存中的数据层
//
// 职责：在已映射好的共享内存上管理数据布局，不碰锁、不碰文件
//
// 包含：
//   - 头部字段：magic/version/entryCount/generation/cleanFlag/crashed
//   - 256 个哈希桶自旋锁（lock + owner PID）
//   - FNV-1a 哈希表（65521 个 slot）
//   - FreeListAlloc 块分配器
//   - 数据块（key-value entry 的实际存储）
//
// 共享内存布局（VERSION 1）：
//   [magic(4) | version(4) | entryCount(4) | generation(4)
//    | cleanFlag(4) | crashed(4)
//    | hashLocks(256*4) | hashOwners(256*4)
//    | hashTable(65521*4)
//    | allocHeader(16)    ← totalBlocks + freeHead + allocLock + allocOwner
//    | blocks(N*64)]
//
// Entry 格式（每个 key-value 对）：
//   [type(1) | keyLen(4) | key(keyLen) | valLen(4) | value(valLen) | nextHash(4)]
//   所有 entry 存储在 FreeListAlloc 分配的 blocks 中
//
// 线程安全：
//   本类不做同步，调用方（Dict）需在外层加锁
//   Hash 链表操作（FindKey/RemoveKey）需要对应的 hash slot 锁
//   分配器操作（Alloc/Free）需要 AllocLock
//   entryCount/generation 修改需要 MetaLock 或外层统一锁
// ─────────────────────────────────────────────────────────────
class DictStore
{
public:
    static const uint32_t MAGIC = 0x5059435A;          // "PYCZ"
    static const uint32_t VERSION = 1;
    static const uint32_t HASH_SLOTS = 65521;           // 哈希表 slot 数
    static const uint32_t HASH_LOCK_POOL = 256;         // 哈希自旋锁数（2 的幂）

    DictStore();

    // ── 初始化 ──
    // base: 共享内存基址（MapViewOfFile 返回值）
    // totalSize: 共享内存总大小
    // initialize: true=首次初始化（清零所有自旋锁）
    void Init(void* base, uint32_t totalSize, bool initialize);

    // ── 头部字段访问 ──
    bool  IsValid() const;       // magic == MAGIC && version == VERSION？
    bool  IsDirty() const;       // generation>0 但 cleanFlag==0（上次异常退出）？
    void  MarkClean() { *m_cleanFlag = 1; }
    void  MarkCrashed() { *m_crashed = 1; }

    uint32_t EntryCount() const { return m_entryCount ? *m_entryCount : 0; }
    void     SetEntryCount(uint32_t n) { if (m_entryCount) *m_entryCount = n; }
    void     IncEntryCount() { if (m_entryCount) (*m_entryCount)++; }
    void     DecEntryCount() { if (m_entryCount && *m_entryCount > 0) (*m_entryCount)--; }

    uint32_t Generation() const { return m_generation ? *m_generation : 0; }
    void     TickGeneration() { if (m_generation) (*m_generation)++; }

    uint32_t* CrashedPtr() const { return m_crashed; }

    uint32_t AllocOffset() const { return m_allocOffset; }

    // ── Hash 链表操作（调用方需持有对应 slot 的锁） ──
    // FindKey: 遍历 hash 链，找 key 对应的 entry
    // RemoveKey: 从 hash 链中移除指定 key 的 entry
    // LinkEntry: 将新 entry 链入 hash 表头部
    // CollectKeys: 收集所有 key（用于 Keys()）
    char* FindKey(const std::string& key) const;
    void  RemoveKey(const std::string& key);
    uint32_t SlotOf(const std::string& key) const;
    uint32_t BlockOffset(char* ptr) const { return (uint32_t)(ptr - m_blockBase); }
    void     LinkEntry(uint32_t slot, uint32_t blockOff,
                       uint32_t keyLen, uint32_t valLen);
    void     CollectKeys(std::vector<std::string>& out) const;

    // ── 分配器 ──
    FreeListAlloc& Allocator() { return m_alloc; }

    // ── 清空（调用方需持有所有锁） ──
    void Clear();

    // ── 自旋锁地址暴露（供 Dict 初始化锁时使用） ──
    volatile LONG* AllocLockAddr() { return m_alloc.LockAddr(); }
    DWORD*         AllocOwnerAddr() { return m_alloc.OwnerAddr(); }

    volatile LONG* HashLockAddr(uint32_t i) {
        return &m_hashLocks[i & (HASH_LOCK_POOL - 1)];
    }
    DWORD* HashOwnerAddr(uint32_t i) {
        return &m_hashOwners[i & (HASH_LOCK_POOL - 1)];
    }

    // ── Entry 大小计算 ──
    // type(1) + keyLen(4) + key + valLen(4) + value + nextHash(4)
    static uint32_t EntrySize(uint32_t keyLen, uint32_t valLen)
    {
        return 9 + keyLen + valLen + 4;
    }

private:
    // ── 共享内存头部指针 ──
    uint32_t* m_magic;
    uint32_t* m_version;
    uint32_t* m_entryCount;
    uint32_t* m_generation;
    uint32_t* m_cleanFlag;
    uint32_t* m_crashed;

    // ── 哈希表 + 分配器 ──
    HashIndex     m_hash;          // FNV-1a 哈希表（65521 slot）
    FreeListAlloc m_alloc;         // 64 字节块粒度分配器
    uint32_t      m_allocOffset;   // allocator 在共享内存中的偏移
    char*         m_blockBase;     // 块数据基址 = base + allocOffset + HEADER_SIZE

    // ── 哈希桶自旋锁池 ──
    volatile LONG* m_hashLocks;    // 256 个 lock word
    DWORD*         m_hashOwners;   // 256 个 owner PID
};

}} // namespace pyczan::shmem

#endif
