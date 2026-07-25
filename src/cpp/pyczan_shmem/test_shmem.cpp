#include "pyczan_shmem/shmem_dict.hpp"
#include <iostream>
#include <cassert>
#include <string>

using namespace pyczan_shmem;

static int s_testCount = 0;
static int s_passCount = 0;

#define TEST(name)                                                    \
    do {                                                              \
        ++s_testCount;                                                \
        std::cout << "[TEST] " << name << " ... ";                    \
    } while (0)

#define PASS()                                                        \
    do {                                                              \
        ++s_passCount;                                                \
        std::cout << "PASS" << std::endl;                             \
    } while (0)

#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) {                                                \
            std::cerr << "FAIL at line " << __LINE__                  \
                      << ": " #cond << std::endl;                     \
            return 1;                                                 \
        }                                                             \
    } while (0)

int main()
{
    // === 测试 1: 打开和关闭 ===
    TEST("OpenOrCreate and Close");
    {
        SharedMemoryDict d;
        CHECK(d.OpenOrCreate("test_dict", 65536));
        CHECK(d.IsOpen());
        CHECK(d.Name() == "test_dict");
        d.Close();
        CHECK(!d.IsOpen());
    }
    PASS();

    // === 测试 2: Set 和 Get ===
    TEST("Set and Get");
    {
        SharedMemoryDict d;
        CHECK(d.OpenOrCreate("test_dict"));
        d.Set("hello", "world");
        CHECK(d.Get("hello") == "world");
        d.Close();
    }
    PASS();

    // === 测试 3: 覆盖已有值 ===
    TEST("Overwrite existing key");
    {
        SharedMemoryDict d;
        CHECK(d.OpenOrCreate("test_dict"));
        d.Set("key", "value1");
        d.Set("key", "value2");
        CHECK(d.Get("key") == "value2");
        d.Close();
    }
    PASS();

    // === 测试 4: Has ===
    TEST("Has");
    {
        SharedMemoryDict d;
        CHECK(d.OpenOrCreate("test_dict"));
        d.Set("exists", "yes");
        CHECK(d.Has("exists"));
        CHECK(!d.Has("nonexistent"));
        d.Close();
    }
    PASS();

    // === 测试 5: Delete ===
    TEST("Delete");
    {
        SharedMemoryDict d;
        CHECK(d.OpenOrCreate("test_dict"));
        d.Set("temp", "value");
        CHECK(d.Has("temp"));
        CHECK(d.Delete("temp"));
        CHECK(!d.Has("temp"));
        CHECK(!d.Delete("nonexistent"));
        d.Close();
    }
    PASS();

    // === 测试 6: Size ===
    TEST("Size");
    {
        SharedMemoryDict d;
        CHECK(d.OpenOrCreate("test_dict"));
        CHECK(d.Size() == 0);
        d.Set("a", "1");
        d.Set("b", "2");
        d.Set("c", "3");
        CHECK(d.Size() == 3);
        d.Clear();
        CHECK(d.Size() == 0);
        d.Close();
    }
    PASS();

    // === 测试 7: Clear ===
    TEST("Clear");
    {
        SharedMemoryDict d;
        CHECK(d.OpenOrCreate("test_dict"));
        d.Set("x", "10");
        d.Set("y", "20");
        CHECK(d.Size() == 2);
        d.Clear();
        CHECK(d.Size() == 0);
        CHECK(!d.Has("x"));
        d.Close();
    }
    PASS();

    // === 测试 8: Keys ===
    TEST("Keys");
    {
        SharedMemoryDict d;
        CHECK(d.OpenOrCreate("test_dict"));
        d.Set("alpha", "1");
        d.Set("beta", "2");
        d.Set("gamma", "3");
        auto keys = d.Keys();
        CHECK(keys.size() == 3);
        bool foundAlpha = false, foundBeta = false, foundGamma = false;
        for (const auto& k : keys)
        {
            if (k == "alpha") foundAlpha = true;
            if (k == "beta") foundBeta = true;
            if (k == "gamma") foundGamma = true;
        }
        CHECK(foundAlpha && foundBeta && foundGamma);
        d.Close();
    }
    PASS();

    // === 测试 9: 未打开时 Size 和 Has 返回 0/false ===
    TEST("Not open returns empty");
    {
        SharedMemoryDict d;
        CHECK(d.Size() == 0);
        CHECK(!d.Has("anything"));
        CHECK(d.Keys().empty());
    }
    PASS();

    std::cout << std::endl;
    std::cout << "Results: " << s_passCount << "/" << s_testCount
              << " tests passed." << std::endl;

    return (s_passCount == s_testCount) ? 0 : 1;
}
