#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include "pyczan_shmem/shmem_dict.hpp"

namespace nb = nanobind;
using namespace pyczan_shmem;

NB_MODULE(_pyczan_shmem, m)
{
    nb::class_<SharedMemoryDict>(m, "SharedMemoryDict")
        .def(nb::init<>())
        .def("OpenOrCreate", &SharedMemoryDict::OpenOrCreate,
            nb::arg("name"), nb::arg("size") = 65536,
            "Open existing shared memory or create a new one")
        .def("Close", &SharedMemoryDict::Close,
            "Close the shared memory segment")
        .def("Set", &SharedMemoryDict::Set,
            "Set a key-value pair")
        .def("Get",
            [](SharedMemoryDict& d, const std::string& key) -> std::string
            {
                try
                {
                    return d.Get(key);
                }
                catch (const std::out_of_range& e)
                {
                    throw nb::key_error(e.what());
                }
            },
            "Get value by key")
        .def("Delete", &SharedMemoryDict::Delete,
            "Delete a key-value pair")
        .def("Has", &SharedMemoryDict::Has,
            "Check if key exists")
        .def("Size", &SharedMemoryDict::Size,
            "Get number of entries")
        .def("Clear", &SharedMemoryDict::Clear,
            "Clear all entries")
        .def("Keys", &SharedMemoryDict::Keys,
            "Get all keys")
        .def("Name", &SharedMemoryDict::Name,
            "Get the shared memory name")
        .def("IsOpen", &SharedMemoryDict::IsOpen,
            "Check if shared memory is open")
        .def("__setitem__",
            [](SharedMemoryDict& d, const std::string& key,
                const std::string& value)
            {
                d.Set(key, value);
            })
        .def("__getitem__",
            [](SharedMemoryDict& d, const std::string& key) -> std::string
            {
                try
                {
                    return d.Get(key);
                }
                catch (const std::out_of_range& e)
                {
                    throw nb::key_error(e.what());
                }
            })
        .def("__delitem__",
            [](SharedMemoryDict& d, const std::string& key)
            {
                if (!d.Delete(key))
                {
                    throw nb::key_error("Key not found: " + key);
                }
            })
        .def("__contains__",
            [](SharedMemoryDict& d, const std::string& key) -> bool
            {
                return d.Has(key);
            })
        .def("__len__",
            [](SharedMemoryDict& d) -> std::size_t
            {
                return d.Size();
            })
        .def("__enter__",
            [](SharedMemoryDict& d) -> SharedMemoryDict&
            {
                return d;
            })
        .def("__exit__",
            [](SharedMemoryDict& d, nb::args)
            {
                d.Close();
            });
}
