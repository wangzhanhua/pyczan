#include "pyczan_simd/simd_ops.hpp"
#include <cstring>
#include <algorithm>
#include <immintrin.h>  // SSE/AVX 内联函数

namespace pyczan_simd {

std::size_t CountChar(const std::string& text, char ch)
{
    const char* data = text.data();
    std::size_t len = text.size();
    std::size_t count = 0;

    // 使用 SSE2 每次处理 16 字节
    __m128i target = _mm_set1_epi8(ch);
    std::size_t i = 0;

    for (; i + 16 <= len; i += 16)
    {
        __m128i chunk = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(data + i));
        __m128i cmp = _mm_cmpeq_epi8(chunk, target);
        // 提取比较结果的掩码
        int mask = _mm_movemask_epi8(cmp);
        count += static_cast<std::size_t>(__popcnt(static_cast<unsigned>(mask)));
    }

    // 处理剩余字节
    for (; i < len; i++)
    {
        if (data[i] == ch) count++;
    }

    return count;
}

std::string Trim(const std::string& text)
{
    const char* data = text.data();
    std::size_t len = text.size();
    std::size_t start = 0;
    std::size_t end = len;

    // 前向跳过空白
    while (start < len)
    {
        char c = data[start];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        start++;
    }

    // 后向跳过空白
    while (end > start)
    {
        char c = data[end - 1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        end--;
    }

    return text.substr(start, end - start);
}

std::string ReplaceChar(const std::string& text, char oldChar, char newChar)
{
    std::string result = text;
    char* data = &result[0];
    std::size_t len = result.size();

    // 使用 SSE2 每次处理 16 字节
    __m128i oldVal = _mm_set1_epi8(oldChar);
    __m128i newVal = _mm_set1_epi8(newChar);
    std::size_t i = 0;

    for (; i + 16 <= len; i += 16)
    {
        __m128i chunk = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(data + i));
        __m128i cmp = _mm_cmpeq_epi8(chunk, oldVal);
        // 将匹配的字节替换为新值
        __m128i blended = _mm_blendv_epi8(chunk, newVal, cmp);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(data + i), blended);
    }

    // 处理剩余字节
    for (; i < len; i++)
    {
        if (data[i] == oldChar) data[i] = newChar;
    }

    return result;
}

} // namespace pyczan_simd
