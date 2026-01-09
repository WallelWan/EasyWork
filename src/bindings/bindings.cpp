#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "runtime/core_tbb.h"
#include "runtime/node_registry.h"
#include "runtime/modules.h"
#include "runtime/memory/frame.h"
#include "runtime/type_system.h"

// Avoid 'using namespace' in header files or global scope to prevent ambiguity
namespace py = pybind11;

PYBIND11_MODULE(easywork_core, m) {
    m.doc() = "EasyWork Core (TBB + OpenCV) with C++20 Factory Pattern + Type System";

    // ========== Type System ==========
    // TypeInfo 绑定
    py::class_<easywork::TypeInfo>(m, "TypeInfo")
        .def_readonly("name", &easywork::TypeInfo::type_name)
        .def("__eq__", &easywork::TypeInfo::operator==)
        .def("__ne__", &easywork::TypeInfo::operator!=)
        .def("__repr__", [](const easywork::TypeInfo& self) {
            return "<TypeInfo: " + self.type_name + ">";
        });

    // NodeTypeInfo 绑定
    py::class_<easywork::NodeTypeInfo>(m, "NodeTypeInfo")
        .def_property_readonly("input_types", [](const easywork::NodeTypeInfo& self) {
            std::vector<easywork::TypeInfo> types;
            for (const auto& t : self.input_types) {
                types.push_back(t);
            }
            return types;
        })
        .def_property_readonly("output_types", [](const easywork::NodeTypeInfo& self) {
            std::vector<easywork::TypeInfo> types;
            for (const auto& t : self.output_types) {
                types.push_back(t);
            }
            return types;
        })
        .def("accepts_input", &easywork::NodeTypeInfo::accepts_input)
        .def("output_matches", &easywork::NodeTypeInfo::output_matches);

    // ========== Core Classes ==========
    py::class_<easywork::ExecutionGraph>(m, "ExecutionGraph")
        .def(py::init<>())
        .def("reset", &easywork::ExecutionGraph::Reset);

    py::class_<easywork::Executor>(m, "Executor")
        .def(py::init<>())
        .def("run", &easywork::Executor::run,
             py::call_guard<py::gil_scoped_release>());

    // ========== Node Base Class ==========
    py::class_<easywork::Node, std::shared_ptr<easywork::Node>>(m, "Node")
        .def("build", &easywork::Node::build)
        .def("connect", &easywork::Node::connect)
        .def("activate", &easywork::Node::Activate)
        .def("set_input", &easywork::Node::set_input)
        .def_property_readonly("type_info", &easywork::Node::get_type_info);

    // ========== Factory (C++20 Auto-Registration) ==========
    py::class_<easywork::NodeRegistry>(m, "_NodeRegistry")
        .def_static("instance", &easywork::NodeRegistry::instance,
                    py::return_value_policy::reference)
        .def("create", &easywork::NodeRegistry::Create)
        .def("registered_nodes", &easywork::NodeRegistry::RegisteredNodes)
        .def("is_registered", &easywork::NodeRegistry::IsRegistered);

    m.def("create_node", [](const std::string& name,
                            py::args args,
                            py::kwargs kwargs) {
        return easywork::NodeRegistry::instance().Create(name, args, kwargs);
    }, py::arg("name"), "Create a node by registered name using the factory pattern");

    // ========== Frame (Zero-Copy Buffer Protocol) ==========
    py::class_<easywork::FrameBuffer, std::shared_ptr<easywork::FrameBuffer>>(
        m, "Frame", py::buffer_protocol())
        .def_readonly("width", &easywork::FrameBuffer::width)
        .def_readonly("height", &easywork::FrameBuffer::height)
        .def_buffer([](easywork::FrameBuffer &f) -> py::buffer_info {
            // Buffer Protocol: Return info about the underlying memory
            return py::buffer_info(
                f.data,                               /* Pointer to buffer */
                sizeof(unsigned char),                /* Size of one scalar */
                py::format_descriptor<unsigned char>::format(), /* Python struct-style format descriptor */
                3,                                    /* Number of dimensions (H, W, C) */
                { f.height, f.width, 3 },             /* Buffer dimensions */
                { (size_t)f.stride, (size_t)3, sizeof(unsigned char) } /* Strides (in bytes) */
            );
        });

    // ========== PyFuncNode (Manual Binding Only) ==========
    // Note: PyFuncNode is NOT registered in the factory as it requires Python callables
    py::class_<easywork::PyFuncNode, easywork::Node,
               std::shared_ptr<easywork::PyFuncNode>>(m, "PyFuncNode")
        .def(py::init<py::function>())
        .def("build", &easywork::PyFuncNode::build)
        .def("set_input", &easywork::PyFuncNode::set_input);

    // Note: All other nodes (CameraSource, CannyFilter, NullSink, VideoWriterSink)
    // are automatically available through the factory pattern.
    // Use ew.module.CameraSource(), ew.module.CannyFilter(), etc. in Python.
}
