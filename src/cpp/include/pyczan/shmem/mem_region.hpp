#ifndef PYCZAN_SHMEM_MEM_REGION_H_
#define PYCZAN_SHMEM_MEM_REGION_H_

#include <string>
#include <cstdint>
#include <windows.h>

namespace pyczan { namespace shmem {

// ─────────────────────────────────────────────────────────────
// SharedMemoryRegion — 共享内存文件映射
//
// 职责：管理 CreateFileMapping / MapViewOfFile 的生命周期
//       "给我一块共享内存，我不管里面装什么"
//
// 文件位置：%TEMP%\pyczan_<name>.bin
// 命名规则：基于 name 构造文件名和文件映射名
//
// 使用流程：
//   OpenOrCreate(name, size)  → 返回 bool（true=新建）
//   Base()                     → 获取映射基址
//   ... 读写共享内存 ...
//   Close()                    → 卸载映射
//
// 进程安全：
//   同一个 name 在多个进程中调用 OpenOrCreate 会映射同一块内存
//   跨进程读写需要配合锁（SharedSpinLock / KernelMutex）
//
// 注意：
//   文件映射大小在创建时固定，不可动态扩展
//   Reset() 删除文件，应在所有进程 Close 后调用
// ─────────────────────────────────────────────────────────────
class SharedMemoryRegion
{
public:
    SharedMemoryRegion();
    ~SharedMemoryRegion();

    // 打开或创建共享内存文件
    // 返回 true = 新建了文件，false = 打开了已有文件
    bool OpenOrCreate(const std::string& name, uint32_t size);

    // 关闭所有句柄（UnmapViewOfFile + CloseHandle）
    void Close();

    // 映射基址（MapViewOfFile 返回值），映射后固定不变
    void* Base() const { return m_pView; }

    // 映射大小，创建时指定
    uint32_t Size() const { return m_size; }

    // 删除共享内存文件（磁盘文件）
    static void Reset(const std::string& name);

private:
    static std::string _FilePath(const std::string& name);

    HANDLE    m_hFile;      // 文件句柄（INVALID_HANDLE_VALUE 表示未打开）
    HANDLE    m_hMapping;   // 文件映射句柄
    LPVOID    m_pView;      // MapViewOfFile 返回的基址（nullptr 表示未映射）
    uint32_t  m_size;       // 映射大小
    std::string m_name;     // 映射名称
};

}} // namespace pyczan::shmem

#endif
