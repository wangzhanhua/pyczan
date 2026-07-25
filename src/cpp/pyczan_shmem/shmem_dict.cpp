#include "pyczan_shmem/shmem_dict.hpp"
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace pyczan_shmem {

const DWORD SharedMemoryDict::HEADER_MAGIC = 0x53484D44;
const std::string SharedMemoryDict::SHMEM_PREFIX = "pyczan_shmem_";
const std::string SharedMemoryDict::MUTEX_PREFIX = "pyczan_shmem_mtx_";

SharedMemoryDict::SharedMemoryDict()
    : m_hFileMapping(nullptr)
    , m_pView(nullptr)
    , m_hMutex(nullptr)
    , m_size(0)
{
}

SharedMemoryDict::~SharedMemoryDict()
{
    Close();
}

bool SharedMemoryDict::OpenOrCreate(const std::string& name, std::size_t size)
{
    if (m_hFileMapping != nullptr)
    {
        return true;
    }

    m_name = name;
    m_size = size;

    std::string shmemName = SHMEM_PREFIX + name;
    std::string mutexName = MUTEX_PREFIX + name;

    // 创建或打开共享内存
    m_hFileMapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,   // 使用系统分页文件
        nullptr,                // 默认安全属性
        PAGE_READWRITE,         // 读写权限
        0,                      // 高位大小
        static_cast<DWORD>(size), // 低位大小
        shmemName.c_str()       // 名称
    );

    DWORD createFileMappingErr = GetLastError();  // 立即保存错误码

    if (m_hFileMapping == nullptr)
    {
        m_name.clear();
        throw std::runtime_error("Failed to create/open shared memory: "
            + std::to_string(createFileMappingErr));
    }

    // 映射到进程地址空间
    m_pView = MapViewOfFile(
        m_hFileMapping,
        FILE_MAP_ALL_ACCESS,    // 完全访问权限
        0, 0,                   // 偏移
        0                       // 映射整个文件
    );

    if (m_pView == nullptr)
    {
        DWORD err = GetLastError();
        CloseHandle(m_hFileMapping);
        m_hFileMapping = nullptr;
        m_name.clear();
        throw std::runtime_error("Failed to map shared memory: "
            + std::to_string(err));
    }

    // 如果是新创建的，初始化头部
    if (createFileMappingErr != ERROR_ALREADY_EXISTS)
    {
        Header* header = GetHeader();
        header->magic    = HEADER_MAGIC;
        header->capacity = static_cast<DWORD>(size) - sizeof(Header);
        header->dataSize = 0;
        header->count    = 0;
    }
    else
    {
        // 验证 magic
        Header* header = GetHeader();
        if (header->magic != HEADER_MAGIC)
        {
            Close();
            throw std::runtime_error("Shared memory has invalid magic number");
        }
    }

    // 创建或打开互斥体
    m_hMutex = CreateMutexA(
        nullptr,            // 默认安全属性
        FALSE,              // 初始不拥有
        mutexName.c_str()   // 名称
    );

    if (m_hMutex == nullptr)
    {
        DWORD err = GetLastError();
        Close();
        throw std::runtime_error("Failed to create/open mutex: "
            + std::to_string(err));
    }

    return true;
}

void SharedMemoryDict::Close()
{
    m_name.clear();

    if (m_pView != nullptr)
    {
        UnmapViewOfFile(m_pView);
        m_pView = nullptr;
    }

    if (m_hFileMapping != nullptr)
    {
        CloseHandle(m_hFileMapping);
        m_hFileMapping = nullptr;
    }

    if (m_hMutex != nullptr)
    {
        CloseHandle(m_hMutex);
        m_hMutex = nullptr;
    }
}

bool SharedMemoryDict::Lock(DWORD timeout)
{
    if (m_hMutex == nullptr)
    {
        return false;
    }

    DWORD result = WaitForSingleObject(m_hMutex, timeout);
    return result == WAIT_OBJECT_0;
}

void SharedMemoryDict::Unlock()
{
    if (m_hMutex != nullptr)
    {
        ReleaseMutex(m_hMutex);
    }
}

std::vector<SharedMemoryDict::Entry> SharedMemoryDict::ReadEntries()
{
    std::vector<Entry> entries;
    Header* header = GetHeader();
    char* data = GetData();
    DWORD offset = 0;

    for (DWORD i = 0; i < header->count; i++)
    {
        if (offset + sizeof(DWORD) > header->dataSize)
        {
            break;
        }
        DWORD keyLen = *reinterpret_cast<DWORD*>(data + offset);
        offset += sizeof(DWORD);

        if (offset + keyLen > header->dataSize)
        {
            break;
        }
        std::string key(data + offset, keyLen);
        offset += keyLen;

        if (offset + sizeof(DWORD) > header->dataSize)
        {
            break;
        }
        DWORD valLen = *reinterpret_cast<DWORD*>(data + offset);
        offset += sizeof(DWORD);

        if (offset + valLen > header->dataSize)
        {
            break;
        }
        std::string value(data + offset, valLen);
        offset += valLen;

        entries.push_back({key, value});
    }

    return entries;
}

void SharedMemoryDict::WriteEntries(const std::vector<Entry>& entries)
{
    Header* header = GetHeader();
    char* data = GetData();
    DWORD offset = 0;

    for (const auto& entry : entries)
    {
        DWORD keyLen = static_cast<DWORD>(entry.key.size());
        DWORD valLen = static_cast<DWORD>(entry.value.size());

        *reinterpret_cast<DWORD*>(data + offset) = keyLen;
        offset += sizeof(DWORD);
        std::memcpy(data + offset, entry.key.data(), keyLen);
        offset += keyLen;

        *reinterpret_cast<DWORD*>(data + offset) = valLen;
        offset += sizeof(DWORD);
        std::memcpy(data + offset, entry.value.data(), valLen);
        offset += valLen;
    }

    header->dataSize = offset;
    header->count = static_cast<DWORD>(entries.size());
}

int SharedMemoryDict::FindEntry(const std::vector<Entry>& entries,
                                const std::string& key)
{
    for (std::size_t i = 0; i < entries.size(); i++)
    {
        if (entries[i].key == key)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SharedMemoryDict::Set(const std::string& key, const std::string& value)
{
    if (m_pView == nullptr)
    {
        throw std::runtime_error("SharedMemoryDict is not open");
    }

    if (!Lock())
    {
        throw std::runtime_error("Failed to lock shared memory");
    }

    try
    {
        auto entries = ReadEntries();
        int idx = FindEntry(entries, key);
        if (idx >= 0)
        {
            entries[idx].value = value;
        }
        else
        {
            entries.push_back({key, value});
        }
        WriteEntries(entries);
        Unlock();
    }
    catch (...)
    {
        Unlock();
        throw;
    }
}

std::string SharedMemoryDict::Get(const std::string& key)
{
    if (m_pView == nullptr)
    {
        throw std::runtime_error("SharedMemoryDict is not open");
    }

    if (!Lock())
    {
        throw std::runtime_error("Failed to lock shared memory");
    }

    try
    {
        auto entries = ReadEntries();
        Unlock();

        int idx = FindEntry(entries, key);
        if (idx < 0)
        {
            throw std::out_of_range("Key not found: " + key);
        }
        return entries[idx].value;
    }
    catch (...)
    {
        Unlock();
        throw;
    }
}

bool SharedMemoryDict::Delete(const std::string& key)
{
    if (m_pView == nullptr)
    {
        throw std::runtime_error("SharedMemoryDict is not open");
    }

    if (!Lock())
    {
        throw std::runtime_error("Failed to lock shared memory");
    }

    try
    {
        auto entries = ReadEntries();
        int idx = FindEntry(entries, key);
        if (idx < 0)
        {
            Unlock();
            return false;
        }
        entries.erase(entries.begin() + idx);
        WriteEntries(entries);
        Unlock();
        return true;
    }
    catch (...)
    {
        Unlock();
        throw;
    }
}

bool SharedMemoryDict::Has(const std::string& key)
{
    if (m_pView == nullptr)
    {
        return false;
    }

    if (!Lock())
    {
        return false;
    }

    try
    {
        auto entries = ReadEntries();
        Unlock();
        return FindEntry(entries, key) >= 0;
    }
    catch (...)
    {
        Unlock();
        return false;
    }
}

std::size_t SharedMemoryDict::Size()
{
    if (m_pView == nullptr)
    {
        return 0;
    }

    if (!Lock())
    {
        return 0;
    }

    try
    {
        Header* header = GetHeader();
        DWORD count = header->count;
        Unlock();
        return count;
    }
    catch (...)
    {
        Unlock();
        return 0;
    }
}

void SharedMemoryDict::Clear()
{
    if (m_pView == nullptr)
    {
        return;
    }

    if (!Lock())
    {
        throw std::runtime_error("Failed to lock shared memory");
    }

    try
    {
        Header* header = GetHeader();
        header->dataSize = 0;
        header->count = 0;
        Unlock();
    }
    catch (...)
    {
        Unlock();
        throw;
    }
}

std::vector<std::string> SharedMemoryDict::Keys()
{
    std::vector<std::string> keys;

    if (m_pView == nullptr)
    {
        return keys;
    }

    if (!Lock())
    {
        return keys;
    }

    try
    {
        auto entries = ReadEntries();
        Unlock();

        for (const auto& entry : entries)
        {
            keys.push_back(entry.key);
        }
        return keys;
    }
    catch (...)
    {
        Unlock();
        return keys;
    }
}

} // namespace pyczan_shmem
