#ifndef PYCZAN_SHMEM_LOCK_H_
#define PYCZAN_SHMEM_LOCK_H_

#include <cstdint>
#include <windows.h>

namespace pyczan { namespace shmem { namespace synch {

// ─────────────────────────────────────────────────────────────
// SharedSpinLock — 共享内存自旋锁
//
// 用途：跨进程同步，lock word 和 owner PID 存放在共享内存中，
//       多个进程通过映射同一块共享内存来使用同一把锁。
//
// 特点：
//   - 纯用户态操作（InterlockedExchange），无内核切换
//   - 自旋等待（_mm_pause），适合短临界区
//   - 内置 PID 崩溃恢复：自旋超过 8192 次后检查持有者进程
//     是否存活（OpenProcess），若已死则窃取锁
//
// 用法：
//   1. 在共享内存中分配 lock word + owner PID 的空间
//   2. 调用 Init(lockAddr, ownerAddr) 设置地址
//   3. 多进程共享同一地址即可互斥
//
// 注意：
//   自旋锁不区分 WAIT_ABANDONED，不提供 LockTimeout
//   如果临界区较长（> 几十微秒），请用 KernelMutex
// ─────────────────────────────────────────────────────────────
class SharedSpinLock
{
public:
    SharedSpinLock() : m_lock(nullptr), m_owner(nullptr) {}

    // 设置锁字段在共享内存中的位置
    void Init(volatile LONG* lockAddr, DWORD* ownerAddr);

    // 获取锁：InterlockedExchange 循环 + PID 崩溃恢复
    void Lock();

    // 释放锁：清 owner → MemoryBarrier → InterlockedExchange(0)
    void Unlock();

    bool IsInitialized() const { return m_lock != nullptr; }

private:
    volatile LONG* m_lock;   // 指向共享内存中的 lock word（0=未锁, 1=已锁）
    DWORD*         m_owner;  // 指向共享内存中的 owner PID
};

}}} // namespace pyczan::shmem::synch

#endif
