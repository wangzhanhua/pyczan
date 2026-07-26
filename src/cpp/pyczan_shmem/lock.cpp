#include "pyczan/shmem/lock.hpp"
#include <intrin.h>

namespace pyczan { namespace shmem { namespace synch {

void SharedSpinLock::Init(volatile LONG* lockAddr, DWORD* ownerAddr)
{
    m_lock = lockAddr;
    m_owner = ownerAddr;
}

// ─────────────────────────────────────────────────────────────
// Lock — 获取共享内存自旋锁
//
// 算法：
//   1. InterlockedExchange(m_lock, 1) 原子交换
//      - 返回 0 → 锁是空闲的，本进程拿到了锁
//      - 返回 1 → 锁被其他进程持有，继续自旋
//   2. 拿到锁后记录本进程 PID，用于崩溃恢复
//   3. MemoryBarrier 防止编译器/CPU 重排后续操作
//
// 崩溃恢复：
//   自旋超过 8192 次后，检查 m_owner 指向的进程是否存活
//   OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)：
//     - 返回 NULL → 持有者已死 → 窃取锁（重新竞争）
//     - 返回句柄 → 持有者还在 → 继续自旋
// ─────────────────────────────────────────────────────────────
void SharedSpinLock::Lock()
{
    DWORD myPid = GetCurrentProcessId();
    DWORD spinCount = 0;

    for (;;)
    {
        // 尝试原子获取锁
        if (InterlockedExchange(m_lock, 1) == 0)
        {
            // 拿到锁，记录 PID 供崩溃恢复使用
            *m_owner = myPid;
            MemoryBarrier();
            return;
        }

        // 自旋等待，每 8192 次检测一次持有者是否存活
        if (++spinCount > 8192)
        {
            DWORD ownedBy = *m_owner;
            if (ownedBy != 0 && ownedBy != myPid)
            {
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                           FALSE, ownedBy);
                if (hProc == NULL)
                {
                    // 持有者进程已死 → 清空 owner，重新竞争
                    *m_owner = 0;
                    spinCount = 0;
                    continue;
                }
                CloseHandle(hProc);
            }
            spinCount = 0;
        }
        // 暂停 CPU 提示（超线程优化 + 省电）
        _mm_pause();
    }
}

// ─────────────────────────────────────────────────────────────
// Unlock — 释放共享内存自旋锁
//
// 顺序很重要：
//   1. 先清 owner（让其他进程的崩溃检测不误判）
//   2. MemoryBarrier（保证 owner 清零在 unlock 之前全局可见）
//   3. InterlockedExchange(m_lock, 0) 释放锁
// ─────────────────────────────────────────────────────────────
void SharedSpinLock::Unlock()
{
    *m_owner = 0;
    MemoryBarrier();
    InterlockedExchange(m_lock, 0);
}

}}} // namespace pyczan::shmem::synch
