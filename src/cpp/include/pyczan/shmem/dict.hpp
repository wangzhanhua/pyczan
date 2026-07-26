#ifndef PYCZAN_SHMEM_DICT_H_
#define PYCZAN_SHMEM_DICT_H_

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "mem_region.hpp"
#include "lock.hpp"
#include "dict_store.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <windows.h>

namespace pyczan { namespace shmem {

// ─────────────────────────────────────────────────────────────
// LockTimeoutError — 锁超时异常
//
// 原版使用 kernel mutex 时有超时机制
// 改为自旋锁后不再抛出此异常（自旋锁无超时概念）
// 保留 LockTimeoutError 类型是为了 Python 绑定兼容
// 当前不会被抛出，但代码中保留抛出的地方也不会执行到
// ─────────────────────────────────────────────────────────────
struct LockTimeoutError : std::runtime_error
{
    explicit LockTimeoutError(const std::string& msg) : std::runtime_error(msg) {}
};

// ─────────────────────────────────────────────────────────────
// Dict — 跨进程共享字典
//
// 核心功能：
//   多个进程通过相同的 name 打开同一块共享内存，
//   像操作普通 dict 一样读写数据，进程间自动同步。
//
// 架构（从底到顶）：
//   SharedMemoryRegion — 文件映射管理（CreateFileMapping）
//   SharedSpinLock — 跨进程自旋锁（256 个哈希锁 + 1 个分配器锁）
//   DictStore — 数据布局（哈希表 + FreeListAlloc）
//
// 锁方案：
//   哈希桶级锁：256 个自旋锁，按 slot & 255 分发
//               不同 slot 的访问并行，同 slot 互斥
//   AllocLock：1 个自旋锁，保护 FreeListAlloc
//   Size()：无锁，原子读 entryCount
//
// 锁顺序（防死锁）：
//   HashLock(slot) → AllocLock
//   Clear / Keys：先锁所有 256 个哈希锁，再锁 AllocLock
//
// 崩溃恢复：
//   每个自旋锁内置 PID 恢复：自旋超过 8192 次后，
//   检查持有者进程是否存活，若已死则窃取锁
//   数据一致性由自旋锁的原子操作保证
//
// Entry 格式：
//   [type(1) | keyLen(4) | key(keyLen) | valLen(4) | value(valLen) | nextHash(4)]
//   type 用于 Python 对象的序列化标记（str/int/float/bytes/pickle）
// ─────────────────────────────────────────────────────────────
class Dict
{
public:
    Dict();
    ~Dict();

    // ── 生命周期 ──
    bool OpenOrCreate(const std::string& name, uint32_t totalSize = 1024 * 1024 * 1024);
    void Close();
    static void Reset(const std::string& name);

    // ── 基础字典操作 ──
    void Set(const std::string& key, const std::string& value, uint8_t type = 0);
    std::string Get(const std::string& key, uint8_t* outType = nullptr);
    bool Delete(const std::string& key);
    bool Has(const std::string& key);
    uint32_t Size();
    void Clear();
    std::vector<std::string> Keys();

    // ── 跨进程通知 ──
    bool Wait(uint32_t timeout = 5000);

    // ── 原子计数器（锁内完成读-改-写） ──
    int64_t Increment(const std::string& key);
    int64_t Decrement(const std::string& key);
    double  AtomicAdd(const std::string& key, double delta);

    // ── 零拷贝缓冲区 ──
    void* Alloc(uint32_t bytes);
    void  Free(void* ptr);

    // ── 状态监控 ──
    struct StatusInfo
    {
        uint32_t entries;         // 当前 entry 数（Size 的返回值）
        uint32_t totalBlocks;     // 总块数
        uint32_t usedBlocks;      // 已用块数
        uint32_t freeFragments;   // 空闲碎片数
        uint32_t lockContention;  // 锁竞争计数（当前为 0，暂未追踪）
        uint32_t generation;      // 写入次数计数器
        bool     wasCrashed;      // 上次是否异常退出
    };
    StatusInfo Status();

    bool IsOpen() const { return m_region.Base() != nullptr; }
    const std::string& Name() const { return m_name; }

private:
    // ── 四层模块 ──
    SharedMemoryRegion    m_region;       // Layer 1: 文件映射
    DictStore             m_store;        // Layer 3: 数据布局 + 锁池
    synch::SharedSpinLock m_allocLock;    // Layer 2: 分配器自旋锁

    HANDLE      m_hEvent;       // 变更通知事件对象
    std::string m_name;
    uint32_t    m_lockContention;

    // ── 哈希桶锁辅助 ──
    void _HashLock(uint32_t slot);
    void _HashUnlock(uint32_t slot);
    uint32_t _Slot(const std::string& key) const { return m_store.SlotOf(key); }

    // ── 全清工具（持有所有锁时调用） ──
    void _ClearAll();

    // 锁顺序：HashLock(slot) → AllocLock（防死锁）
};

}} // namespace pyczan::shmem

#endif
