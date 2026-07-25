#ifndef PYCZAN_SHMEM_DICT_H_
#define PYCZAN_SHMEM_DICT_H_

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "freelist.hpp"
#include "hash_index.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

namespace pyczan {
namespace shmem {

class Dict
{
public:
    Dict();
    ~Dict();

    bool OpenOrCreate(const std::string& name, uint32_t totalSize = 1024 * 1024 * 1024);
    void Close();

    void Set(const std::string& key, const std::string& value, uint8_t type = 0);
    std::string Get(const std::string& key, uint8_t* outType = nullptr);
    bool Delete(const std::string& key);
    bool Has(const std::string& key);
    uint32_t Size();
    void Clear();
    std::vector<std::string> Keys();

    // 零拷贝缓冲区
    void* Alloc(uint32_t bytes);
    void  Free(void* ptr);

    // 强制重置共享内存（丢弃所有数据）
    static void Reset(const std::string& name);

    // 状态监控
    struct StatusInfo {
        uint32_t entries;
        uint32_t totalBlocks;
        uint32_t usedBlocks;
        uint32_t freeFragments;
        uint32_t lockContention;
        uint32_t generation;
        bool     wasCrashed;
    };
    StatusInfo Status();

    bool IsOpen() const { return m_hMutex != nullptr; }
    const std::string& Name() const { return m_name; }

private:
    static const uint32_t HASH_SLOTS = 65521;

    HANDLE      m_hMutex;
    HANDLE      m_hEvent;
    std::string m_name;
    HANDLE      m_hMapping;
    LPVOID      m_pView;
    uint32_t    m_totalSize;

    // 共享内存布局：
    // [entryCount(4) | generation(4) | cleanFlag(4) | hash(HASH_SLOTS*4) | allocHeader(8) | blocks(N*64)]
    uint32_t*   m_entryCount;
    uint32_t*   m_generation;
    uint32_t*   m_cleanFlag;
    HashIndex   m_hash;
    FreeListAlloc m_alloc;
    uint32_t    m_allocOffset;
    uint32_t    m_lockContention;

    bool Lock(uint32_t timeout = 5000);
    void Unlock();
};

} // namespace shmem
} // namespace pyczan

#endif
