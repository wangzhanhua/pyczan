#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include "pyczan/shmem/dict.hpp"
#include <Python.h>

namespace nb = nanobind;
using namespace pyczan::shmem;

// 将 Python 对象序列化为 (type, bytes)
static std::string serialize(nb::handle obj, uint8_t& type)
{
    PyObject* pyObj = obj.ptr();
    if (PyUnicode_Check(pyObj)) {
        type = 0;
        Py_ssize_t len;
        const char* data = PyUnicode_AsUTF8AndSize(pyObj, &len);
        return std::string(data, len);
    }
    if (PyLong_Check(pyObj)) {
        type = 1;
        PyObject* s = PyObject_Str(pyObj);
        Py_ssize_t len;
        const char* data = PyUnicode_AsUTF8AndSize(s, &len);
        std::string r(data, len);
        Py_DECREF(s);
        return r;
    }
    if (PyFloat_Check(pyObj)) {
        type = 2;
        PyObject* s = PyObject_Str(pyObj);
        Py_ssize_t len;
        const char* data = PyUnicode_AsUTF8AndSize(s, &len);
        std::string r(data, len);
        Py_DECREF(s);
        return r;
    }
    if (PyBytes_Check(pyObj)) {
        type = 3;
        char* data;
        Py_ssize_t len;
        PyBytes_AsStringAndSize(pyObj, &data, &len);
        return std::string(data, len);
    }
    // 其他类型：pickle
    type = 4;
    auto pickle = nb::module_::import_("pickle");
    auto dumped = pickle.attr("dumps")(obj);
    char* buf;
    Py_ssize_t len;
    PyBytes_AsStringAndSize(dumped.ptr(), &buf, &len);
    return std::string(buf, len);
}

// 反序列化 (type, bytes) → Python 对象
static nb::object deserialize(const std::string& data, uint8_t type)
{
    switch (type) {
        case 0:  // str
            return nb::cast(data);
        case 1:  // int
            return nb::steal<nb::object>(PyLong_FromString(data.c_str(), nullptr, 0));
        case 2:  // float
            return nb::cast(std::stod(data));
        case 3:  // bytes
            return nb::bytes(data.data(), data.size());
        case 4: { // pickle
            auto pickle = nb::module_::import_("pickle");
            auto bytes = nb::bytes(data.data(), data.size());
            return nb::steal<nb::object>(
                PyObject_CallMethod(pickle.ptr(), "loads", "(O)", bytes.ptr()));
        }
        default:
            return nb::cast(data);
    }
}

NB_MODULE(_shmem, m)
{
    m.doc() = "pyczan shared memory module";

    nb::class_<Dict>(m, "Dict")
        .def(nb::init<>())
        .def("OpenOrCreate", &Dict::OpenOrCreate,
            nb::arg("name"), nb::arg("total_size") = 1024 * 1024 * 1024)
        .def("Close", &Dict::Close)

        // Set: 接受任意 Python 类型
        .def("Set", [](Dict& d, const std::string& key, nb::object val) {
            uint8_t type;
            std::string data = serialize(val, type);
            d.Set(key, data, type);
        })
        // Get: 返回原始 Python 类型
        .def("Get", [](Dict& d, const std::string& key) -> nb::object {
            try {
                uint8_t type = 0;
                std::string data = d.Get(key, &type);
                return deserialize(data, type);
            } catch (const std::out_of_range& e) {
                throw nb::key_error(e.what());
            }
        })

        .def("Delete", &Dict::Delete)
        .def("Has", &Dict::Has)
        .def("Size", &Dict::Size)
        .def("Clear", &Dict::Clear)
        .def("Keys", &Dict::Keys)
        .def("IsOpen", &Dict::IsOpen)
        .def("Name", &Dict::Name)

        // 强制重置
        .def_static("Reset", &Dict::Reset)

        // 状态监控
        .def("Status", [](Dict& d) -> nb::dict {
            auto s = d.Status();
            nb::dict r;
            r["entries"] = s.entries;
            r["total_blocks"] = s.totalBlocks;
            r["used_blocks"] = s.usedBlocks;
            r["free_fragments"] = s.freeFragments;
            r["lock_contention"] = s.lockContention;
            r["generation"] = s.generation;
            r["was_crashed"] = s.wasCrashed;
            return r;
        })

        // 零拷贝缓冲区
        .def("Alloc", [](Dict& d, uint32_t bytes) -> nb::object {
            void* ptr = d.Alloc(bytes);
            if (!ptr) return nb::none();
            return nb::steal<nb::object>(
                PyMemoryView_FromMemory((char*)ptr, bytes, PyBUF_WRITE));
        })
        .def("Free", [](Dict& d, nb::object view) {
            Py_buffer pybuf;
            if (PyObject_GetBuffer(view.ptr(), &pybuf, PyBUF_WRITABLE) < 0)
                throw std::runtime_error("Invalid buffer");
            d.Free(pybuf.buf);
            PyBuffer_Release(&pybuf);
        })

        // Python dict protocol
        .def("__setitem__", [](Dict& d, const std::string& key, nb::object val) {
            uint8_t type;
            std::string data = serialize(val, type);
            d.Set(key, data, type);
        })
        .def("__getitem__", [](Dict& d, const std::string& key) -> nb::object {
            try {
                uint8_t type = 0;
                std::string data = d.Get(key, &type);
                return deserialize(data, type);
            } catch (const std::out_of_range& e) {
                throw nb::key_error(e.what());
            }
        })
        .def("__delitem__", [](Dict& d, const std::string& key) {
            if (!d.Delete(key))
                throw nb::key_error("Key not found: " + key);
        })
        .def("__contains__", [](Dict& d, const std::string& key) { return d.Has(key); })
        .def("__len__", [](Dict& d) { return d.Size(); })
        .def("__enter__", [](Dict& d) -> Dict& { return d; })
        .def("__exit__", [](Dict& d, nb::args) { d.Close(); });
}
