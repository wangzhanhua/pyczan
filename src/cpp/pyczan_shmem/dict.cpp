#include "pyczan/shmem/dict.hpp"
#include <cstring>
#include <algorithm>
#include <intrin.h>

namespace pyczan { namespace shmem {

// ════════════════════════════════════════════════════════════════
// 构造 / 析构
// ════════════════════════════════════════════════════════════════

Dict::Dict()
    : m_hEvent(nullptr), m_lockContention(0)
{
}

Dict::~Dict()
{
    Close();
}

// ════════════════════════════════════════════════════════════════
// 哈希桶级自旋锁
// ════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────
// _HashLock / _HashUnlock — 按 hash slot 加解锁
//
// 65521 个 hash slot 共享 256 个自旋锁，分配方式：
//   lockIndex = slot & (HASH_LOCK_POOL - 1)
// 因为 HASH_LOCK_POOL=256=2^8，取模等于 &255
//
// 每个自旋锁有独立的 lock word 和 owner PID，
// 都在共享内存中，不同进程的 _HashLock 操作同一块内存
//
// 锁实现与 SharedSpinLock 相同（InterlockedExchange + PID 恢复）
// 这里内联实现是为了避免每个哈希锁创建一个 SharedSpinLock 对象
// ─────────────────────────────────────────────────────────────
void Dict::_HashLock(uint32_t slot)
{
    uint32_t i = slot & (DictStore::HASH_LOCK_POOL - 1);
    volatile LONG* lock = m_store.HashLockAddr(i);
    DWORD* owner = m_store.HashOwnerAddr(i);
    DWORD myPid = GetCurrentProcessId();
    DWORD spinCount = 0;

    for (;;)
    {
        // 原子交换：lock=1 表示已锁
        if (InterlockedExchange(lock, 1) == 0)
        {
            *owner = myPid;           // 记录 PID 供崩溃恢复
            MemoryBarrier();
            return;
        }

        // 超过 8192 次自旋后检查持有者是否存活
        if (++spinCount > 8192)
        {
            DWORD ownedBy = *owner;
            if (ownedBy != 0 && ownedBy != myPid)
            {
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                           FALSE, ownedBy);
                if (hProc == NULL)
                {
                    // 持有者已死 → 窃取锁
                    *owner = 0;
                    spinCount = 0;
                    continue;
                }
                CloseHandle(hProc);
            }
            spinCount = 0;
        }
        _mm_pause();
    }
}

void Dict::_HashUnlock(uint32_t slot)
{
    uint32_t i = slot & (DictStore::HASH_LOCK_POOL - 1);
    volatile LONG* lock = m_store.HashLockAddr(i);
    DWORD* owner = m_store.HashOwnerAddr(i);
    // 清 owner → 内存屏障 → 释放锁（顺序不可颠倒）
    *owner = 0;
    MemoryBarrier();
    InterlockedExchange(lock, 0);
}

// ════════════════════════════════════════════════════════════════
// OpenOrCreate / Close / Reset
// ════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────
// OpenOrCreate — 打开或创建共享内存字典
//
// 流程：
//   1. SharedMemoryRegion::OpenOrCreate 创建/打开文件映射
//   2. DictStore::Init 在映射的内存上布置数据布局
//   3. SharedSpinLock::Init 设置分配器自旋锁的地址
//   4. CreateEvent 创建进程间事件通知
//   5. 新建 → 直接返回（Init 已初始化所有字段）
//   6. 版本不兼容 → 全清重建
//   7. 上次非正常关闭 → 标记 crashed
// ─────────────────────────────────────────────────────────────
bool Dict::OpenOrCreate(const std::string& name, uint32_t totalSize)
{
    if (m_region.Base()) return true;  // 已打开
    m_name = name;

    // Layer 1: 文件映射（CreateFileMapping + MapViewOfFile）
    bool isNew = m_region.OpenOrCreate(name, totalSize);
    if (!m_region.Base())
        throw std::runtime_error("OpenOrCreate: mapping failed");

    // Layer 3: 在映射好的内存上布置数据结构
    // 这会设置头字段指针、哈希表、分配器、自旋锁池
    m_store.Init(m_region.Base(), totalSize, isNew);

    // Layer 2: 分配器自旋锁
    // lock word 和 owner PID 在 FreeListAlloc::Header 中
    m_allocLock.Init(m_store.AllocLockAddr(), m_store.AllocOwnerAddr());

    // 进程间事件通知（跨进程 signal）
    m_hEvent = CreateEventA(NULL, FALSE, FALSE,
                            ("pyczan_shmem_evt_" + name).c_str());

    if (isNew) { m_lockContention = 0; return true; }

    // ── 版本不兼容 → 全清 ──
    if (!m_store.IsValid())
    {
        _ClearAll();
        m_lockContention = 0;
        return true;
    }

    // ── 上次非正常关闭 → 只标记 crashed，不做自动恢复 ──
    // 因为自旋锁的 PID 恢复机制保证了锁最终会被释放，
    // 数据大概率是完整的，不需要自动清空
    if (m_store.IsDirty())
        *m_store.CrashedPtr() = 1;

    m_lockContention = 0;
    return true;
}

// ─────────────────────────────────────────────────────────────
// Close — 关闭字典
//
// 标记 clean 关闭，释放事件对象句柄
// 不 Unmap 共享内存（由 SharedMemoryRegion::Close 处理）
// 不删除共享内存文件（磁盘文件保留）
//
// 注意：Close 后再次 OpenOrCreate 会重新映射同一块内存
// ─────────────────────────────────────────────────────────────
void Dict::Close()
{
    if (!m_region.Base()) return;

    // 标记 clean 关闭（供下次 OpenOrCreate 的崩溃检测使用）
    if (m_store.CrashedPtr())
        *m_store.CrashedPtr() = 1;

    if (m_hEvent) { CloseHandle(m_hEvent); m_hEvent = nullptr; }
    m_region.Close();
    m_name.clear();
}

// ── Reset：删除共享内存文件（所有进程 Close 后调用） ──
void Dict::Reset(const std::string& name)
{
    SharedMemoryRegion::Reset(name);
}

// ════════════════════════════════════════════════════════════════
// 内部工具
// ════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────
// _ClearAll — 全清所有数据
//
// 清零哈希表、重置分配器、写入头部字段、清零所有自旋锁
// 调用方需持有所有 256 个哈希锁 + AllocLock
// ─────────────────────────────────────────────────────────────
void Dict::_ClearAll()
{
    m_store.Clear();

    // 重置分配器
    uint32_t aOff = m_store.AllocOffset();
    uint32_t blockBytes = (m_region.Size() > aOff + FreeListAlloc::HEADER_SIZE)
        ? (m_region.Size() - aOff - FreeListAlloc::HEADER_SIZE) / 64 : 0;
    m_store.Allocator().Init(
        static_cast<char*>(m_region.Base()) + aOff, blockBytes, true);

    // 重置头部字段（magic/version/entryCount/generation/cleanFlag/crashed）
    uint32_t* hdr = static_cast<uint32_t*>(m_region.Base());
    hdr[0] = DictStore::MAGIC;
    hdr[1] = DictStore::VERSION;
    hdr[2] = 0;  // entryCount
    hdr[3] = 0;  // generation
    hdr[4] = 1;  // cleanFlag = 1（clean）
    hdr[5] = 1;  // crashed = 1（标记为 crash 检测过）

    // 清零所有自旋锁
    *m_store.AllocLockAddr() = 0;
    *m_store.AllocOwnerAddr() = 0;
    for (uint32_t i = 0; i < DictStore::HASH_LOCK_POOL; i++)
    {
        *m_store.HashLockAddr(i) = 0;
        *m_store.HashOwnerAddr(i) = 0;
    }
}

// ════════════════════════════════════════════════════════════════
// 基础字典操作
// 锁顺序：HashLock(slot) → AllocLock（防死锁）
// ════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────
// Set — 写入 key-value
//
// 流程：
//   1. 加 HashLock(slot) + AllocLock
//   2. 查找旧值 → 如果存在则删除（RemoveKey + Alloc::Free）
//   3. Alloc 分配新 entry 空间
//   4. 填入 type/keyLen/key/valLen/value
//   5. LinkEntry 链入 hash 表（头插法）
//   6. 更新 entryCount / generation
//   7. SetEvent 通知等待的进程
// ─────────────────────────────────────────────────────────────
void Dict::Set(const std::string& key, const std::string& value, uint8_t type)
{
    if (!m_region.Base()) throw std::runtime_error("Dict not open");

    uint32_t slot = _Slot(key);
    _HashLock(slot);
    m_allocLock.Lock();

    // 1. 若有旧值则删除
    char* old = m_store.FindKey(key);
    if (old)
    {
        m_store.RemoveKey(key);
        m_store.Allocator().Free(old);
        m_store.DecEntryCount();
    }

    // 2. 计算所需空间并分配
    uint32_t kl = (uint32_t)key.size();
    uint32_t vl = (uint32_t)value.size();
    uint32_t esize = DictStore::EntrySize(kl, vl);
    char* buf = (char*)m_store.Allocator().Alloc(esize);
    if (!buf) { m_allocLock.Unlock(); _HashUnlock(slot); throw std::runtime_error("OOM"); }

    // 3. 写入 entry：type(1) | keyLen(4) | key | valLen(4) | value | nextHash(4)
    buf[0] = (char)type;
    memcpy(buf + 1, &kl, 4);
    memcpy(buf + 5, key.data(), kl);
    memcpy(buf + 5 + kl, &vl, 4);
    memcpy(buf + 9 + kl, value.data(), vl);

    // 4. 链入 hash 表头部
    m_store.LinkEntry(slot, m_store.BlockOffset(buf), kl, vl);

    // 5. 更新元数据
    m_store.IncEntryCount();
    m_store.TickGeneration();

    m_allocLock.Unlock();
    _HashUnlock(slot);
    if (m_hEvent) SetEvent(m_hEvent);  // 通知 Wait 中的进程
}

// ─────────────────────────────────────────────────────────────
// Get — 读取 key 对应的 value
//
// 只需要 HashLock(slot)，不需要 AllocLock
// 因为 FindKey 只是遍历 hash 链读数据，不碰分配器
// 不同 slot 的 Get 可以完全并行
// ─────────────────────────────────────────────────────────────
std::string Dict::Get(const std::string& key, uint8_t* outType)
{
    if (!m_region.Base()) throw std::runtime_error("Dict not open");

    uint32_t slot = _Slot(key);
    _HashLock(slot);

    char* ptr = m_store.FindKey(key);
    if (!ptr) { _HashUnlock(slot); throw std::out_of_range("Key not found: " + key); }

    if (outType) *outType = (uint8_t)ptr[0];
    uint32_t kl = *(uint32_t*)(ptr + 1);
    uint32_t vl = *(uint32_t*)(ptr + 5 + kl);
    std::string r((const char*)(ptr + 9 + kl), vl);

    _HashUnlock(slot);
    return r;
}

// ─────────────────────────────────────────────────────────────
// Delete — 删除 key
//
// HashLock 找到并移除节点，然后 AllocLock 释放内存
// 锁顺序：HashLock → AllocLock
// ─────────────────────────────────────────────────────────────
bool Dict::Delete(const std::string& key)
{
    if (!m_region.Base()) throw std::runtime_error("Dict not open");

    uint32_t slot = _Slot(key);
    _HashLock(slot);

    char* ptr = m_store.FindKey(key);
    if (!ptr) { _HashUnlock(slot); return false; }

    m_store.RemoveKey(key);

    // 释放 entry 占用的 blocks
    m_allocLock.Lock();
    m_store.Allocator().Free(ptr);
    m_allocLock.Unlock();

    m_store.DecEntryCount();
    m_store.TickGeneration();

    _HashUnlock(slot);
    return true;
}

// ── Has：只需 HashLock(slot)，不碰分配器 ──
bool Dict::Has(const std::string& key)
{
    if (!m_region.Base()) return false;

    uint32_t slot = _Slot(key);
    _HashLock(slot);
    bool found = m_store.FindKey(key) != nullptr;
    _HashUnlock(slot);
    return found;
}

// ── Size：无锁，原子读 entryCount（x64 上 4 字节对齐读天然原子） ──
uint32_t Dict::Size()
{
    if (!m_region.Base()) return 0;
    return m_store.EntryCount();
}

// ─────────────────────────────────────────────────────────────
// Clear — 清空所有数据
//
// 锁所有 256 个哈希锁 + AllocLock 实现排他
// 锁顺序：先锁所有哈希锁（从 0 到 255），再锁 AllocLock
// 解锁时逆序：先 AllocLock，再哈希锁（从 255 到 0）
// ─────────────────────────────────────────────────────────────
void Dict::Clear()
{
    if (!m_region.Base()) return;

    // 锁所有哈希桶，按序号从 0 到 255（固定顺序防死锁）
    for (uint32_t i = 0; i < DictStore::HASH_LOCK_POOL; i++)
        _HashLock(i);

    m_allocLock.Lock();
    _ClearAll();
    m_allocLock.Unlock();

    // 逆序解锁
    for (uint32_t i = DictStore::HASH_LOCK_POOL; i > 0; i--)
        _HashUnlock(i - 1);
}

// ─────────────────────────────────────────────────────────────
// Keys — 获取所有 key 的列表
//
// 锁所有 256 个哈希桶后遍历整个哈希表
// 遍历期间不会有任何 Set/Delete 修改数据
// ─────────────────────────────────────────────────────────────
std::vector<std::string> Dict::Keys()
{
    std::vector<std::string> keys;
    if (!m_region.Base()) return keys;

    // 锁所有哈希桶 → CollectKeys → 解锁
    for (uint32_t i = 0; i < DictStore::HASH_LOCK_POOL; i++)
        _HashLock(i);

    m_store.CollectKeys(keys);

    for (uint32_t i = DictStore::HASH_LOCK_POOL; i > 0; i--)
        _HashUnlock(i - 1);

    return keys;
}

// ════════════════════════════════════════════════════════════════
// Status / Wait
// ════════════════════════════════════════════════════════════════

// ── Status：读取各项状态指标（部分字段无锁，最佳只读） ──
Dict::StatusInfo Dict::Status()
{
    StatusInfo info = {};
    info.entries = m_store.EntryCount();    // 无锁原子读
    info.generation = m_store.Generation();
    info.lockContention = m_lockContention;  // 当前未追踪，保留为 0
    info.wasCrashed = (m_store.CrashedPtr() && *m_store.CrashedPtr() != 0);

    // Allocator 统计需要 AllocLock
    m_allocLock.Lock();
    info.totalBlocks = m_store.Allocator().TotalBlocks();
    info.usedBlocks = m_store.Allocator().UsedBlocks();
    info.freeFragments = m_store.Allocator().FreeFragments();
    m_allocLock.Unlock();

    return info;
}

// ── Wait：阻塞等待其他进程写入数据（基于 Windows Event 对象） ──
bool Dict::Wait(uint32_t timeout)
{
    if (!m_hEvent) return false;
    return WaitForSingleObject(m_hEvent, timeout) == WAIT_OBJECT_0;
}

// ════════════════════════════════════════════════════════════════
// 零拷贝缓冲区
// ════════════════════════════════════════════════════════════════

// Alloc/Free 只走 AllocLock，不走哈希锁（不涉及 hash 表操作）
void* Dict::Alloc(uint32_t bytes)
{
    if (!m_region.Base()) return nullptr;
    m_allocLock.Lock();
    void* p = m_store.Allocator().Alloc(bytes);
    m_allocLock.Unlock();
    return p;
}

void Dict::Free(void* ptr)
{
    if (!ptr || !m_region.Base()) return;
    m_allocLock.Lock();
    m_store.Allocator().Free(ptr);
    m_allocLock.Unlock();
}

// ════════════════════════════════════════════════════════════════
// 计数器辅助函数
// ════════════════════════════════════════════════════════════════

// 从 entry 中读取 int64 值（type=5 二进制 int64，或 type=1 字符串遗留格式）
static int64_t _ReadInt64(char* ptr)
{
    if (!ptr) return 0;
    uint8_t t = (uint8_t)ptr[0];
    uint32_t kl = *(uint32_t*)(ptr + 1);
    uint32_t vl = *(uint32_t*)(ptr + 5 + kl);
    if (t == 5 && vl >= 8) { int64_t v; memcpy(&v, ptr + 9 + kl, 8); return v; }
    if (t == 1) { std::string s((const char*)(ptr + 9 + kl), vl);
        try { return std::stoll(s); } catch (...) { return 0; } }
    return 0;
}

// 从 entry 中读取 double 值（type=6 二进制 double，或 type=2 字符串遗留格式）
static double _ReadDouble(char* ptr)
{
    if (!ptr) return 0.0;
    uint8_t t = (uint8_t)ptr[0];
    uint32_t kl = *(uint32_t*)(ptr + 1);
    uint32_t vl = *(uint32_t*)(ptr + 5 + kl);
    if (t == 6 && vl >= 8) { double v; memcpy(&v, ptr + 9 + kl, 8); return v; }
    if (t == 2) { std::string s((const char*)(ptr + 9 + kl), vl);
        try { return std::stod(s); } catch (...) { return 0.0; } }
    return 0.0;
}

// 写入计数器 entry（二进制格式，int64=type5，double=type6）
static void _WriteCounter(char* buf, const std::string& key,
                          uint32_t kl, uint32_t vl,
                          const void* val, uint8_t type)
{
    buf[0] = (char)type;
    memcpy(buf + 1, &kl, 4); memcpy(buf + 5, key.data(), kl);
    memcpy(buf + 5 + kl, &vl, 4); memcpy(buf + 9 + kl, val, vl);
}

// ── 通用计数器 RMW：读旧值 → 删旧 → 写新（HashLock + AllocLock 内调用） ──
static void _CounterRMW(DictStore& store, FreeListAlloc& alloc,
                         const std::string& key, uint32_t slot,
                         const void* val, uint32_t valBytes, uint8_t type)
{
    // 删除旧值
    char* old = store.FindKey(key);
    if (old)
    {
        store.RemoveKey(key);
        alloc.Free(old);
        store.DecEntryCount();
    }
    // 分配并写入新值
    uint32_t kl = (uint32_t)key.size();
    uint32_t esize = DictStore::EntrySize(kl, valBytes);
    char* buf = (char*)alloc.Alloc(esize);
    if (!buf) throw std::runtime_error("OOM");
    _WriteCounter(buf, key, kl, valBytes, val, type);
    store.LinkEntry(slot, store.BlockOffset(buf), kl, valBytes);
    store.IncEntryCount();
    store.TickGeneration();
}

// ════════════════════════════════════════════════════════════════
// Increment / Decrement / AtomicAdd
// 锁顺序：HashLock(slot) → AllocLock
// ════════════════════════════════════════════════════════════════

int64_t Dict::Increment(const std::string& key)
{
    if (!m_region.Base()) throw std::runtime_error("Dict not open");
    uint32_t slot = _Slot(key);

    _HashLock(slot);
    m_allocLock.Lock();
    try
    {
        // 读旧值 + 1 → 写回（整个 read-modify-write 在锁内完成）
        int64_t val = _ReadInt64(m_store.FindKey(key)) + 1;
        _CounterRMW(m_store, m_store.Allocator(), key, slot, &val, 8, 5);
        m_allocLock.Unlock();
        _HashUnlock(slot);
        if (m_hEvent) SetEvent(m_hEvent);
        return val;
    }
    catch (...) { m_allocLock.Unlock(); _HashUnlock(slot); throw; }
}

int64_t Dict::Decrement(const std::string& key)
{
    if (!m_region.Base()) throw std::runtime_error("Dict not open");
    uint32_t slot = _Slot(key);

    _HashLock(slot);
    m_allocLock.Lock();
    try
    {
        int64_t val = _ReadInt64(m_store.FindKey(key)) - 1;
        _CounterRMW(m_store, m_store.Allocator(), key, slot, &val, 8, 5);
        m_allocLock.Unlock();
        _HashUnlock(slot);
        if (m_hEvent) SetEvent(m_hEvent);
        return val;
    }
    catch (...) { m_allocLock.Unlock(); _HashUnlock(slot); throw; }
}

double Dict::AtomicAdd(const std::string& key, double delta)
{
    if (!m_region.Base()) throw std::runtime_error("Dict not open");
    uint32_t slot = _Slot(key);

    _HashLock(slot);
    m_allocLock.Lock();
    try
    {
        double val = _ReadDouble(m_store.FindKey(key)) + delta;
        _CounterRMW(m_store, m_store.Allocator(), key, slot, &val, 8, 6);
        m_allocLock.Unlock();
        _HashUnlock(slot);
        if (m_hEvent) SetEvent(m_hEvent);
        return val;
    }
    catch (...) { m_allocLock.Unlock(); _HashUnlock(slot); throw; }
}

}} // namespace pyczan::shmem
