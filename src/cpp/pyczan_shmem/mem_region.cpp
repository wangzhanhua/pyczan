#include "pyczan/shmem/mem_region.hpp"
#include <cstring>

namespace pyczan { namespace shmem {

SharedMemoryRegion::SharedMemoryRegion()
    : m_hFile(INVALID_HANDLE_VALUE), m_hMapping(nullptr)
    , m_pView(nullptr), m_size(0)
{
}

SharedMemoryRegion::~SharedMemoryRegion()
{
    Close();
}

// 计算共享内存文件的磁盘路径：%TEMP%\pyczan_<name>.bin
std::string SharedMemoryRegion::_FilePath(const std::string& name)
{
    char tmp[MAX_PATH + 1];
    DWORD len = GetTempPathA(MAX_PATH + 1, tmp);
    std::string path(tmp, len);
    // 去掉末尾的反斜杠
    while (!path.empty() && path.back() == '\\')
        path.pop_back();
    return path + "\\pyczan_" + name + ".bin";
}

// ─────────────────────────────────────────────────────────────
// OpenOrCreate — 打开或创建共享内存
//
// 流程：
//   1. 创建/打开磁盘文件（OPEN_ALWAYS）
//   2. 如果是新建文件，将文件扩展到指定大小
//   3. CreateFileMapping 创建命名映射对象（所有进程共享）
//   4. MapViewOfFile 将映射对象映射到本进程地址空间
//
// 返回 bool 表示是否新建，调用方可根据此值决定是否初始化内容
// ─────────────────────────────────────────────────────────────
bool SharedMemoryRegion::OpenOrCreate(const std::string& name, uint32_t size)
{
    if (m_pView) return false;  // 已打开，不重复映射

    m_name = name;
    m_size = size;

    std::string filePath = _FilePath(name);
    m_hFile = CreateFileA(filePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    bool isNew = (GetLastError() == ERROR_ALREADY_EXISTS) ? false : true;

    // 新建文件时需要分配磁盘空间
    if (isNew)
    {
        LARGE_INTEGER li;
        li.QuadPart = size;
        SetFilePointerEx(m_hFile, li, NULL, FILE_BEGIN);
        SetEndOfFile(m_hFile);
    }

    // 创建命名文件映射，所有进程通过相同的 name 共享
    m_hMapping = CreateFileMappingA(m_hFile, NULL, PAGE_READWRITE,
                                    0, size, ("pyczan_shmem_" + name).c_str());
    if (!m_hMapping)
    {
        CloseHandle(m_hFile); m_hFile = INVALID_HANDLE_VALUE;
        return false;
    }

    // 映射到本进程地址空间，不同进程映射的虚拟地址不同
    // 所以共享内存中只能存偏移或索引，不能存指针
    m_pView = MapViewOfFile(m_hMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!m_pView)
    {
        CloseHandle(m_hMapping); m_hMapping = nullptr;
        CloseHandle(m_hFile); m_hFile = INVALID_HANDLE_VALUE;
        return false;
    }

    return isNew;
}

// ─────────────────────────────────────────────────────────────
// Close — 关闭所有句柄
//
// 注意：Close() 后 Base() 返回 nullptr，不能再访问共享内存
// 析构函数自动调用 Close()，即使已经手动调用过也不会重复释放
// ─────────────────────────────────────────────────────────────
void SharedMemoryRegion::Close()
{
    if (m_pView) { UnmapViewOfFile(m_pView); m_pView = nullptr; }
    if (m_hMapping) { CloseHandle(m_hMapping); m_hMapping = nullptr; }
    if (m_hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hFile); m_hFile = INVALID_HANDLE_VALUE;
    }
    m_name.clear();
}

// ─────────────────────────────────────────────────────────────
// Reset — 删除共享内存文件
//
// 应在所有进程都已 Close() 后调用
// 注意：这不会清理命名内核对象（mutex/event），
// 但下次 OpenOrCreate 会重新创建它们
// ─────────────────────────────────────────────────────────────
void SharedMemoryRegion::Reset(const std::string& name)
{
    DeleteFileA(_FilePath(name).c_str());
}

}} // namespace pyczan::shmem
