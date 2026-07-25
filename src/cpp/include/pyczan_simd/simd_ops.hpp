#ifndef SIMD_OPS_H_
#define SIMD_OPS_H_

#include <string>
#include <cstdint>

namespace pyczan_simd {

// 统计字符串中某个字符的出现次数（SSE2 加速）
std::size_t CountChar(const std::string& text, char ch);

// 去除字符串两端的空白字符
std::string Trim(const std::string& text);

// 字符串中单个字符替换（SSE2 加速）
std::string ReplaceChar(const std::string& text, char oldChar, char newChar);

} // namespace pyczan_simd

#endif // SIMD_OPS_H_
