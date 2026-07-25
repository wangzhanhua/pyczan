#ifndef PYCZAN_SHMEM_DICT_H_
#define PYCZAN_SHMEM_DICT_H_

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "block_zone.hpp"
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

    void Set(const std::string& key, const std::string& value);
    std::string Get(const std::string& key);
    bool Delete(const std::string& key);
    bool Has(const std::string& key);
    uint32_t Size();
    void Clear();
    std::vector<std::string> Keys();

    bool IsOpen() const { return m_hMutex != nullptr; }
    const std::string& Name() const { return m_name; }

private:
    static const uint32_t BLOCK_SMALL = 64;
    static const uint32_t BLOCK_LARGE = 1024;
    static const float    SMALL_RATIO;

    HANDLE      m_hMutex;
    HANDLE      m_hEvent;
    std::string m_name;

    BlockZone m_small;
    BlockZone m_large;

    BlockZone& PickZone(const std::string& key, const std::string& value);

    bool Lock(uint32_t timeout = 5000);
    void Unlock();
};

} // namespace shmem
} // namespace pyczan

#endif
