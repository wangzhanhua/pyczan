#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include "pyczan/shmem/dict.hpp"

namespace nb = nanobind;
using namespace pyczan::shmem;

NB_MODULE(_shmem, m)
{
    m.doc() = "pyczan shared memory module";

    nb::class_<Dict>(m, "Dict")
        .def(nb::init<>())
        .def("OpenOrCreate", &Dict::OpenOrCreate,
            nb::arg("name"), nb::arg("total_size") = 1024 * 1024 * 1024,
            "Open or create shared memory segment")
        .def("Close", &Dict::Close,
            "Close shared memory")
        .def("Set",
            [](Dict& d, const std::string& key, const std::string& value)
            {
                d.Set(key, value);
            })
        .def("Get",
            [](Dict& d, const std::string& key) -> std::string
            {
                try { return d.Get(key); }
                catch (const std::out_of_range& e)
                { throw nb::key_error(e.what()); }
            })
        .def("Delete", &Dict::Delete)
        .def("Has", &Dict::Has)
        .def("Size", &Dict::Size)
        .def("Clear", &Dict::Clear)
        .def("Keys", &Dict::Keys)
        .def("IsOpen", &Dict::IsOpen)
        .def("Name", &Dict::Name)

        // Python dict protocol
        .def("__setitem__",
            [](Dict& d, const std::string& key, const std::string& value)
            { d.Set(key, value); })
        .def("__getitem__",
            [](Dict& d, const std::string& key) -> std::string
            {
                try { return d.Get(key); }
                catch (const std::out_of_range& e)
                { throw nb::key_error(e.what()); }
            })
        .def("__delitem__",
            [](Dict& d, const std::string& key)
            {
                if (!d.Delete(key))
                    throw nb::key_error("Key not found: " + key);
            })
        .def("__contains__",
            [](Dict& d, const std::string& key) { return d.Has(key); })
        .def("__len__",
            [](Dict& d) { return d.Size(); })
        .def("__enter__",
            [](Dict& d) -> Dict& { return d; })
        .def("__exit__",
            [](Dict& d, nb::args) { d.Close(); });
}
