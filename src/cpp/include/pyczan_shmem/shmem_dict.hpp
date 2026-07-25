#ifndef SHMEM_DICT_H_
#define SHMEM_DICT_H_

#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

namespace pyczan_shmem {

class SharedMemoryDict
{
public:
    SharedMemoryDict();
    ~SharedMemoryDict();

    bool OpenOrCreate(const std::string& name, std::size_t size = 65536);
    void Close();

    void Set(const std::string& key, const std::string& value);
    std::string Get(const std::string& key);
    bool Delete(const std::string& key);
    bool Has(const std::string& key);
    std::size_t Size();
    void Clear();
    std::vector<std::string> Keys();

    bool IsOpen() const
    {
        return m_hFileMapping != nullptr;
    }

    const std::string& Name() const
    {
        return m_name;
    }

private:
    struct Header
    {
        DWORD magic;
        DWORD capacity;
        DWORD dataSize;
        DWORD count;
    };

    struct Entry
    {
        std::string key;
        std::string value;
    };

    static const DWORD HEADER_MAGIC;
    static const std::string SHMEM_PREFIX;
    static const std::string MUTEX_PREFIX;

    HANDLE      m_hFileMapping;
    LPVOID      m_pView;
    HANDLE      m_hMutex;
    std::string m_name;
    std::size_t m_size;

    bool Lock(DWORD timeout = INFINITE);
    void Unlock();

    Header* GetHeader()
    {
        return static_cast<Header*>(m_pView);
    }

    char* GetData()
    {
        return static_cast<char*>(m_pView) + sizeof(Header);
    }

    std::vector<Entry> ReadEntries();
    void WriteEntries(const std::vector<Entry>& entries);
    int FindEntry(const std::vector<Entry>& entries, const std::string& key);

};

} // namespace pyczan_shmem

#endif // SHMEM_DICT_H_
