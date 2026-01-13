#define EASYWORK_ENABLE_PYBIND
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "runtime/core/core.h"
#include "runtime/registry/node_registry.h"
#include "modules/module_registry.h"
#include "runtime/memory/frame.h"
#include "runtime/types/type_system.h"
#include "runtime/types/type_converter.h"

namespace py = pybind11;

namespace {

void RegisterPythonConverters() {
    easywork::RegisterPythonType<py::object>();
}

std::any ConvertArg(const py::handle& obj, const easywork::TypeInfo& target_type) {
    if (target_type.type_info == &typeid(void)) {
         throw std::runtime_error("Cannot convert argument to 'void' or unknown type.");
    }
    std::any source = py::reinterpret_borrow<py::object>(obj);
    std::any converted = easywork::TypeConverterRegistry::instance().convert(
        source, typeid(py::object), *target_type.type_info);
    if (!converted.has_value()) {
        throw std::runtime_error("No conversion handler for target type: " + target_type.type_name);
    }
    return converted;
}

py::object FromPacket(const easywork::Packet& packet) {
    if (!packet.has_value()) {
        return py::none();
    }
    auto type_info = packet.type();
    const auto& registry = easywork::AnyToPyRegistry();
    auto it = registry.find(type_info.type_index);
    if (it == registry.end()) {
        throw std::runtime_error("No Python converter registered for type: " + type_info.type_name);
    }
    return it->second(packet.data());
}

} // namespace

PYBIND11_MODULE(easywork_core, m) {
    m.doc() = "EasyWork Core (Taskflow + OpenCV) with C++20 Factory Pattern + Type System";

    RegisterPythonConverters();

    m.attr("ID_FORWARD") = easywork::ID_FORWARD;
    m.attr("ID_OPEN") = easywork::ID_OPEN;
    m.attr("ID_CLOSE") = easywork::ID_CLOSE;

    // ========== Type System ==========
    
    py::class_<easywork::TypeInfo>(m, "TypeInfo")
        .def_readonly("name", &easywork::TypeInfo::type_name)
        .def("__eq__", &easywork::TypeInfo::operator==)
        .def("__ne__", &easywork::TypeInfo::operator!=)
        .def("__repr__", [](const easywork::TypeInfo& self) {
            return "<TypeInfo: " + self.type_name + ">";
        });

    py::class_<easywork::MethodInfo>(m, "MethodInfo")
        .def_readonly("input_types", &easywork::MethodInfo::input_types)
        .def_readonly("output_type", &easywork::MethodInfo::output_type)
        .def("__repr__", [](const easywork::MethodInfo& self) {
            std::string s = "([";
            for (size_t i = 0; i < self.input_types.size(); ++i) {
                s += self.input_types[i].type_name;
                if (i < self.input_types.size() - 1) s += ", ";
            }
            s += "]) -> " + self.output_type.type_name;
            return s;
        });

    py::class_<easywork::NodeTypeInfo>(m, "NodeTypeInfo")
        .def_readonly("methods", &easywork::NodeTypeInfo::methods)
        .def("accepts_input", &easywork::NodeTypeInfo::accepts_input)
        .def("output_matches", &easywork::NodeTypeInfo::output_matches);

    // ========== Core Classes ==========

    py::class_<easywork::ExecutionGraph>(m, "ExecutionGraph")
        .def(py::init<>())
        .def("reset", &easywork::ExecutionGraph::Reset);

    py::class_<easywork::Executor>(m, "Executor")
        .def(py::init<>())
        .def("run", &easywork::Executor::run,
             py::call_guard<py::gil_scoped_release>())
        .def("open", &easywork::Executor::open)
        .def("close", &easywork::Executor::close);

    py::class_<easywork::Node::UpstreamConnection>(m, "UpstreamConnection")
        .def_readonly("node", &easywork::Node::UpstreamConnection::node, py::return_value_policy::reference)
        .def_readonly("method_id", &easywork::Node::UpstreamConnection::method_id);

    py::class_<easywork::Node, std::shared_ptr<easywork::Node>>(m, "Node")
        .def("build", &easywork::Node::build)
        .def("connect", &easywork::Node::connect)
        .def("activate", &easywork::Node::Activate)
        .def("open", [](easywork::Node& node, py::args args, py::kwargs kwargs) {
            if (kwargs && kwargs.size() > 0) throw std::runtime_error("Kwargs not supported in Node.open()");
            
            std::vector<easywork::Packet> inputs;
            inputs.reserve(args.size());
            
            auto type_info = node.get_type_info();
            auto it = type_info.methods.find(easywork::ID_OPEN);
            
            if (it != type_info.methods.end() && it->second.input_types.size() == args.size()) {
                for (size_t i = 0; i < args.size(); ++i) {
                    inputs.push_back(easywork::Packet::FromAny(
                        ConvertArg(args[i], it->second.input_types[i]),
                        easywork::Packet::NowNs()
                    ));
                }
            } else {
                for (const auto& arg : args) {
                    inputs.push_back(easywork::Packet::From(py::reinterpret_borrow<py::object>(arg), easywork::Packet::NowNs()));
                }
            }
            node.Open(inputs);
        })
        .def("close", [](easywork::Node& node, py::args args, py::kwargs kwargs) {
            if (kwargs && kwargs.size() > 0) throw std::runtime_error("Kwargs not supported in Node.close()");
            
            std::vector<easywork::Packet> inputs;
            inputs.reserve(args.size());
            
            auto type_info = node.get_type_info();
            auto it = type_info.methods.find(easywork::ID_CLOSE);
            
            if (it != type_info.methods.end() && it->second.input_types.size() == args.size()) {
                for (size_t i = 0; i < args.size(); ++i) {
                    inputs.push_back(easywork::Packet::FromAny(
                        ConvertArg(args[i], it->second.input_types[i]),
                        easywork::Packet::NowNs()
                    ));
                }
            } else {
                for (const auto& arg : args) {
                    inputs.push_back(easywork::Packet::From(py::reinterpret_borrow<py::object>(arg), easywork::Packet::NowNs()));
                }
            }
            node.Close(inputs);
        })
        .def("is_open", &easywork::Node::IsOpen)
        .def("invoke", [](easywork::Node& node, const std::string& method, py::args args) {
            size_t method_id = method == "forward" ? easywork::ID_FORWARD : easywork::hash_string(method);
            
            std::vector<easywork::Packet> inputs;
            inputs.reserve(args.size());
            
            auto type_info = node.get_type_info();
            auto it = type_info.methods.find(method_id);
            
            if (it != type_info.methods.end() && it->second.input_types.size() == args.size()) {
                for (size_t i = 0; i < args.size(); ++i) {
                    inputs.push_back(easywork::Packet::FromAny(
                        ConvertArg(args[i], it->second.input_types[i]),
                        easywork::Packet::NowNs()
                    ));
                }
            } else {
                std::string err = "Method '" + method + "' not found or argument count mismatch.";
                if (it != type_info.methods.end()) {
                    err += " Expected " + std::to_string(it->second.input_types.size()) + 
                           " args, got " + std::to_string(args.size());
                }
                throw std::runtime_error(err);
            }
            
            easywork::Packet result = node.invoke(method_id, inputs);
            
            if (!result.has_value()) {
                return py::object(py::none());
            }
            return FromPacket(result);
        })
        .def("set_input", &easywork::Node::set_input)
        .def("set_input_for", &easywork::Node::set_input_for)
        .def("clear_upstreams", &easywork::Node::ClearUpstreams)
        .def("set_method_order", &easywork::Node::SetMethodOrder)
        .def("set_method_sync", &easywork::Node::SetMethodSync)
        .def("set_method_queue_size", &easywork::Node::SetMethodQueueSize)
        .def_property_readonly("type_name", &easywork::Node::type_name)
        .def_property_readonly("type_info", &easywork::Node::get_type_info)
        .def_property_readonly("exposed_methods", &easywork::Node::exposed_methods)
        .def_property_readonly("upstreams", &easywork::Node::get_upstreams)
        .def_property_readonly("connections", [](const easywork::Node& self) {
            return self.upstreams_;
        });

    // ========== NodeRegistry ==========

    py::class_<easywork::NodeRegistry>(m, "_NodeRegistry")
        .def_static("instance", &easywork::NodeRegistry::instance, py::return_value_policy::reference)
        .def("registered_nodes", &easywork::NodeRegistry::RegisteredNodes)
        .def("is_registered", &easywork::NodeRegistry::IsRegistered);

    m.def("create_node", [](const std::string& name, py::args args, py::kwargs kwargs) {
        return easywork::NodeRegistry::instance().Create(name, args, kwargs);
    });

    // ========== Tuple Helpers ==========

    m.def("create_tuple_get_node", &easywork::CreateTupleGetNode);
    m.def("get_tuple_size", &easywork::GetTupleSize);
    
    // ========== Debugging Helpers (Optional) ==========
    
    m.def("reset_small_tracked_live_count", &easywork::ResetSmallTrackedLiveCount);
    m.def("get_small_tracked_live_count", &easywork::GetSmallTrackedLiveCount);
    m.def("reset_method_dispatch_counts", &easywork::ResetMethodDispatchCounts);
    m.def("get_method_dispatch_counts", []() {
        return std::make_tuple(
            easywork::GetMethodDispatchLeftCount(),
            easywork::GetMethodDispatchRightCount(),
            easywork::GetMethodDispatchForwardCount()
        );
    });
    m.def("get_method_dispatch_order_errors", &easywork::GetMethodDispatchOrderErrorCount);
}