#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "runtime/core.h"
#include "runtime/node_registry.h"
#include "runtime/modules.h"
#include "runtime/memory/frame.h"
#include "runtime/type_system.h"

namespace py = pybind11;

namespace {

// --- Automatic Type Conversion Implementation ---

/**
 * @brief Implements automatic conversion from Python objects to C++ types.
 * 
 * Registered as a hook in TypeSystem to be called by Value::cast.
 * Handles std::vector, std::string, numeric types, etc.
 */
std::any PythonCastImplementation(const std::any& src_data, 
                                 const std::type_info& src_type, 
                                 const std::type_info& target_type) {
    if (src_type != typeid(py::object)) {
        return std::any();
    }

    try {
        const py::object& obj = std::any_cast<const py::object&>(src_data);
        
        if (target_type == typeid(int)) return std::any(obj.cast<int>());
        if (target_type == typeid(int64_t)) return std::any(obj.cast<int64_t>());
        if (target_type == typeid(double)) return std::any(obj.cast<double>());
        if (target_type == typeid(float)) return std::any(obj.cast<float>());
        if (target_type == typeid(bool)) return std::any(obj.cast<bool>());
        if (target_type == typeid(std::string)) return std::any(obj.cast<std::string>());
        
        if (target_type == typeid(std::vector<int>)) return std::any(obj.cast<std::vector<int>>());
        if (target_type == typeid(std::vector<std::string>)) return std::any(obj.cast<std::vector<std::string>>());
        if (target_type == typeid(std::vector<double>)) return std::any(obj.cast<std::vector<double>>());

    } catch (...) {
        // Cast failed
    }
    
    return std::any();
}

// --- Strict Argument Conversion (for Open/Close where types are dynamic) ---

/**
 * @brief Converts Python objects to Value during Open/Close calls.
 * 
 * Since Open/Close arguments are dynamic (std::vector<Packet>), we need to 
 * infer the type from the Python object. Only supports basic types.
 */
easywork::Value StrictToValue(const py::handle& obj) {
    if (obj.is_none()) {
        return easywork::Value();
    }
    if (py::isinstance<py::bool_>(obj)) {
        return easywork::Value(obj.cast<bool>());
    }
    if (py::isinstance<py::int_>(obj)) {
        return easywork::Value(obj.cast<int64_t>());
    }
    if (py::isinstance<py::float_>(obj)) {
        return easywork::Value(obj.cast<double>());
    }
    if (py::isinstance<py::str>(obj)) {
        return easywork::Value(obj.cast<std::string>());
    }
    throw std::runtime_error("Unsupported type for Open/Close arguments. Only basic types (int, float, bool, str) are supported.");
}

// --- Smart Argument Conversion (for Invoke) ---

/**
 * @brief Converts Python objects to Value based on target C++ type.
 * 
 * Used during method invocation where the target type is known from TypeInfo.
 * Supports complex conversions like list -> std::vector.
 */
easywork::Value ConvertArg(const py::handle& obj, const easywork::TypeInfo& target_type) {
    if (target_type.type_info == &typeid(void)) {
         throw std::runtime_error("Cannot convert argument to 'void' or unknown type.");
    }

    try {
        if (*target_type.type_info == typeid(int)) return easywork::Value(obj.cast<int>());
        if (*target_type.type_info == typeid(int64_t)) return easywork::Value(obj.cast<int64_t>());
        if (*target_type.type_info == typeid(double)) return easywork::Value(obj.cast<double>());
        if (*target_type.type_info == typeid(float)) return easywork::Value(obj.cast<float>());
        if (*target_type.type_info == typeid(bool)) return easywork::Value(obj.cast<bool>());
        if (*target_type.type_info == typeid(std::string)) return easywork::Value(obj.cast<std::string>());
        
        if (*target_type.type_info == typeid(std::vector<int>)) 
            return easywork::Value(obj.cast<std::vector<int>>());
        if (*target_type.type_info == typeid(std::vector<std::string>)) 
            return easywork::Value(obj.cast<std::vector<std::string>>());
        if (*target_type.type_info == typeid(std::vector<double>)) 
            return easywork::Value(obj.cast<std::vector<double>>());

        if (*target_type.type_info == typeid(py::object)) {
             return easywork::Value(py::reinterpret_borrow<py::object>(obj));
        }

        throw std::runtime_error("No conversion handler for target type: " + target_type.type_name);

    } catch (const py::cast_error& e) {
        throw std::runtime_error("Failed to convert argument to " + target_type.type_name + ": " + e.what());
    }
}

py::object FromValue(const easywork::Value& val) {
    if (!val.has_value()) {
        return py::none();
    }
    auto type_info = val.type();
    
    if (type_info == easywork::TypeInfo::create<int64_t>()) {
        return py::int_(val.cast<int64_t>());
    }
    if (type_info == easywork::TypeInfo::create<int>()) {
        return py::int_(val.cast<int>());
    }
    if (type_info == easywork::TypeInfo::create<double>()) {
        return py::float_(val.cast<double>());
    }
    if (type_info == easywork::TypeInfo::create<float>()) {
        return py::float_(val.cast<float>());
    }
    if (type_info == easywork::TypeInfo::create<bool>()) {
        return py::bool_(val.cast<bool>());
    }
    if (type_info == easywork::TypeInfo::create<std::string>()) {
        return py::str(val.cast<std::string>());
    }
    
    if (type_info == easywork::TypeInfo::create<std::vector<int>>()) {
        return py::cast(val.cast<std::vector<int>>());
    }
    if (type_info == easywork::TypeInfo::create<std::vector<double>>()) {
        return py::cast(val.cast<std::vector<double>>());
    }
    if (type_info == easywork::TypeInfo::create<std::vector<std::string>>()) {
        return py::cast(val.cast<std::vector<std::string>>());
    }

    try {
        return val.cast<py::object>();
    } catch (...) {
        return py::str("<C++ Value: " + type_info.type_name + ">");
    }
}

} // namespace

PYBIND11_MODULE(easywork_core, m) {
    m.doc() = "EasyWork Core (Taskflow + OpenCV) with C++20 Factory Pattern + Type System";

    easywork::GetPythonCastHook() = PythonCastImplementation;

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
                    inputs.push_back(easywork::Packet::From(
                        ConvertArg(args[i], it->second.input_types[i]), 
                        easywork::Packet::NowNs()
                    ));
                }
            } else {
                for (const auto& arg : args) {
                    inputs.push_back(easywork::Packet::From(StrictToValue(arg), easywork::Packet::NowNs()));
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
                    inputs.push_back(easywork::Packet::From(
                        ConvertArg(args[i], it->second.input_types[i]), 
                        easywork::Packet::NowNs()
                    ));
                }
            } else {
                for (const auto& arg : args) {
                    inputs.push_back(easywork::Packet::From(StrictToValue(arg), easywork::Packet::NowNs()));
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
                    inputs.push_back(easywork::Packet::From(
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
            return FromValue(*result.payload);
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