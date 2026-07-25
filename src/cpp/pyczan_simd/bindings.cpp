#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include "pyczan_simd/simd_ops.hpp"

namespace nb = nanobind;
using namespace pyczan_simd;

NB_MODULE(_pyczan_simd, m)
{
    m.doc() = "SIMD-accelerated string and array operations";

    m.def("count_char", &CountChar,
        nb::arg("text"), nb::arg("ch"),
        "Count occurrences of a character (SSE2 accelerated)");

    m.def("trim", &Trim,
        nb::arg("text"),
        "Remove leading and trailing whitespace");

    m.def("replace_char", &ReplaceChar,
        nb::arg("text"), nb::arg("old_char"), nb::arg("new_char"),
        "Replace all occurrences of a character (SSE2 accelerated)");
}
