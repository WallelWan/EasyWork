#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "runtime/core_tbb.h"
#include "runtime/ops_opencv.h"

// Avoid 'using namespace' in header files or global scope to prevent ambiguity
namespace py = pybind11;

PYBIND11_MODULE(easywork_core, m) {
    m.doc() = "EasyWork Core (TBB + OpenCV)";

    // 0. FrameBuffer
    py::class_<easywork::FrameBuffer, std::shared_ptr<easywork::FrameBuffer>>(m, "Frame", py::buffer_protocol())
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

    // 1. Core Classes
    py::class_<easywork::ExecutionGraph>(m, "ExecutionGraph")
        .def(py::init<>());

    py::class_<easywork::Executor>(m, "Executor")
        .def(py::init<>())
        .def("run", &easywork::Executor::run);

    // 2. Node Bases
    py::class_<easywork::Node, std::shared_ptr<easywork::Node>>(m, "Node")
        .def("connect", &easywork::Node::connect);
    
    // 3. Concrete Operators
    
    // CameraSource
    py::class_<easywork::CameraSource, easywork::Node, std::shared_ptr<easywork::CameraSource>>(m, "CameraSource")
        .def(py::init<int>())
        .def("build", &easywork::CameraSource::build)
        .def("set_limit", &easywork::CameraSource::set_limit); // Connect inherited from Node

    // CannyFilter
    py::class_<easywork::CannyFilter, easywork::Node, std::shared_ptr<easywork::CannyFilter>>(m, "CannyFilter")
        .def(py::init<>())
        .def("build", &easywork::CannyFilter::build)
        .def("set_input", &easywork::CannyFilter::set_input); // Connect inherited

    // NullSink
    py::class_<easywork::NullSink, easywork::Node, std::shared_ptr<easywork::NullSink>>(m, "NullSink")
        .def(py::init<>())
        .def("build", &easywork::NullSink::build)
        .def("set_input", &easywork::NullSink::set_input);

    // VideoWriterSink
    py::class_<easywork::VideoWriterSink, easywork::Node, std::shared_ptr<easywork::VideoWriterSink>>(m, "VideoWriterSink")
        .def(py::init<std::string>())
        .def("build", &easywork::VideoWriterSink::build)
        .def("set_input", &easywork::VideoWriterSink::set_input);

    // PyFuncNode
    py::class_<easywork::PyFuncNode, easywork::Node, std::shared_ptr<easywork::PyFuncNode>>(m, "PyFuncNode")
        .def(py::init<py::function>())
        .def("build", &easywork::PyFuncNode::build)
        .def("set_input", &easywork::PyFuncNode::set_input);
}
