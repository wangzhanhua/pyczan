#ifndef PYCZAN_SHMEM_BLOCK_ZONE_H_
#define PYCZAN_SHMEM_BLOCK_ZONE_H_

#include "bitmap.hpp"
#include "hash_index.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

namespace pyczan {
namespace shmem {

// 单个存储区域：独立的共享内存 + 位图 + 哈希表 + 块数组
// 块索引从 1 开始，无偏移，纯本地
class BlockZone
{
public:
    BlockZone();
    ~BlockZone();

    // 创建或打开共享内存
    void Open(const std::string& name, uint32_t blockSize, uint32_t totalSize, bool& isNew);
    void Close();

    // 条目数
    uint32_t EntryCount() const;
    void SetEntryCount(uint32_t count);

    // 核心操作
    void Set(const std::string& key, const std::string& value, bool notify);
    std::string Get(const std::string& key);
    bool Delete(const std::string& key, bool notify);
    bool Has(const std::string& key);
    void Clear();
    std::vector<std::string> Keys();
    uint32_t BlockSize() const { return m_blockSize; }

private:
    static const uint32_t HASH_SLOTS = 65521;

    HANDLE        m_hMapping;
    LPVOID        m_pView;
    LPVOID        m_blocks;
    BitmapAllocator m_bitmap;
    HashIndex     m_hash;
    uint32_t      m_blockSize;
    uint32_t      m_blockCount;

    // 共享内存布局：
    // [entryCount(4) | reserved(4) | hash(HASH_SLOTS*4) | bitmap(qwords*8) | blocks]
    uint32_t* EntryCountPtr() const;
    uint32_t  HashBytes() const;
    uint32_t  BitmapBytes() const;
    uint32_t  HeaderBytes() const;

    // 块操作
    static uint32_t Payload(uint32_t bs) { return bs - 8; }
    void*  BlockAddr(uint32_t idx) const;
    uint32_t FindKey(const std::string& key) const;
    void   WriteHeader(uint32_t idx, const std::string& key, const std::string& value,
                       uint32_t valOff, uint32_t valLen, uint32_t nextBlock);
    void   WriteData(uint32_t idx, const char* data, uint32_t len, uint32_t nextBlock);
    uint32_t ReadNext(uint32_t idx) const;
    void   SetNext(uint32_t idx, uint32_t next);
    uint32_t ReadHash(uint32_t idx) const;
    void   SetHash(uint32_t idx, uint32_t nextHash);
    void   InsertHash(uint32_t slot, uint32_t idx);
    void   RemoveHash(uint32_t slot, uint32_t idx);
};

} // namespace shmem
} // namespace pyczan

#endif
