#define EASYWORK_ENABLE_PYBIND
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "runtime/core/core.h"
#include "runtime/registry/node_registry.h"
#include "modules/control_nodes.h"
#include "modules/module_registry.h"
#include "runtime/memory/frame.h"
#include "runtime/types/type_system.h"
#include "runtime/types/type_converter.h"

namespace py = pybind11;

namespace {

void RegisterPythonConverters() {
    easywork::RegisterPythonType<py::object>();
}

void RegisterArithmeticConverters() {
    easywork::RegisterArithmeticConversions();
}

bool CanConvertTypes(const easywork::TypeInfo& from, const easywork::TypeInfo& to) {
    if (from == to) {
        return true;
    }
    if (!from.type_info || !to.type_info) {
        return false;
    }
    return easywork::TypeConverterRegistry::instance().has_converter(*from.type_info, *to.type_info);
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

std::vector<easywork::Packet> BuildInputsForMethod(
    easywork::Node& node,
    size_t method_id,
    const std::string& method_name,
    py::args args,
    bool strict) {
    std::vector<easywork::Packet> inputs;
    inputs.reserve(args.size());

    auto type_info = node.get_type_info();
    auto it = type_info.methods.find(method_id);
    if (it != type_info.methods.end()) {
        if (it->second.input_types.size() != args.size()) {
            std::string err = "Method '" + method_name + "' argument count mismatch.";
            err += " Expected " + std::to_string(it->second.input_types.size()) +
                   " args, got " + std::to_string(args.size());
            throw std::runtime_error(err);
        }
        for (size_t i = 0; i < args.size(); ++i) {
            inputs.push_back(easywork::Packet::FromAny(
                ConvertArg(args[i], it->second.input_types[i]),
                easywork::Packet::NowNs()
            ));
        }
        return inputs;
    }

    if (strict) {
        std::string err = "Method '" + method_name + "' not found or argument count mismatch.";
        if (it != type_info.methods.end()) {
            err += " Expected " + std::to_string(it->second.input_types.size()) +
                   " args, got " + std::to_string(args.size());
        }
        throw std::runtime_error(err);
    }

    for (const auto& arg : args) {
        inputs.push_back(easywork::Packet::From(
            py::reinterpret_borrow<py::object>(arg),
            easywork::Packet::NowNs()
        ));
    }
    return inputs;
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

struct PyMethodMeta {
    std::string name;
    size_t arg_count{0};
    std::vector<std::string> arg_names;
    std::vector<py::object> defaults;
    std::vector<bool> has_default;
    bool has_varargs{false};
    bool has_varkw{false};
};

class PyNode : public easywork::Node {
public:
    explicit PyNode(py::object instance)
        : instance_(std::move(instance)) {
        py::gil_scoped_acquire gil;
        type_name_ = py::str(instance_.attr("__class__").attr("__name__"));
        LoadMethods();
    }

    void build(easywork::ExecutionGraph& g) override {
        graph_ = &g;
        task_ = g.taskflow.emplace([this]() { RunDispatch(); });
        task_.name(type_name_);
    }

    void connect() override {
        for (const auto& conn : upstreams_) {
            if (conn.node && !conn.weak) {
                conn.node->get_task().precede(task_);
            }
        }
        PrepareInputConverters();
    }

    easywork::NodeTypeInfo get_type_info() const override {
        easywork::NodeTypeInfo info;
        const auto py_object_type = easywork::TypeInfo::create<py::object>();
        for (const auto& [id, meta] : methods_) {
            easywork::MethodInfo method;
            method.input_types = std::vector<easywork::TypeInfo>(meta.arg_count, py_object_type);
            method.output_type = py_object_type;
            info.methods[id] = std::move(method);
        }
        return info;
    }

    std::string type_name() const override {
        return type_name_;
    }

    std::vector<std::string> exposed_methods() const override {
        return exposed_methods_;
    }

    easywork::Packet invoke(size_t method_id, const std::vector<easywork::Packet>& inputs) override {
        return InvokeMethod(method_id, inputs);
    }

    std::vector<std::string> GetMethodArgNames(size_t method_id) const {
        auto it = methods_.find(method_id);
        if (it == methods_.end()) {
            return {};
        }
        if (it->second.has_varargs || it->second.has_varkw) {
            return {};
        }
        return it->second.arg_names;
    }

    bool MethodHasVarArgs(size_t method_id) const {
        auto it = methods_.find(method_id);
        if (it == methods_.end()) {
            return false;
        }
        return it->second.has_varargs || it->second.has_varkw;
    }

    std::unordered_map<std::string, py::object> GetMethodDefaults(size_t method_id) const {
        std::unordered_map<std::string, py::object> defaults;
        auto it = methods_.find(method_id);
        if (it == methods_.end()) {
            return defaults;
        }
        const auto& meta = it->second;
        for (size_t i = 0; i < meta.arg_names.size(); ++i) {
            if (i < meta.has_default.size() && meta.has_default[i]) {
                defaults.emplace(meta.arg_names[i], meta.defaults[i]);
            }
        }
        return defaults;
    }

    easywork::Packet invoke_python(size_t method_id, py::args args, py::kwargs kwargs) {
        auto it = methods_.find(method_id);
        if (it == methods_.end()) {
            throw std::runtime_error("Method not found in Python node: " + std::to_string(method_id));
        }
        py::gil_scoped_acquire gil;
        py::object callable = instance_.attr(it->second.name.c_str());
        ValidateSignature(callable, args, kwargs);
        py::object result = callable(*args, **kwargs);
        if (result.is_none()) {
            return easywork::Packet::Empty();
        }
        return easywork::Packet::From(result, 0);
    }

private:
    void LoadMethods() {
        std::vector<std::string> method_names;
        if (py::hasattr(instance_, "__ew_methods__")) {
            py::object methods_obj = instance_.attr("__ew_methods__");
            for (const auto& item : methods_obj) {
                method_names.push_back(py::cast<std::string>(item));
            }
        }
        if (method_names.empty()) {
            method_names.push_back("forward");
        }

        for (const auto& name : method_names) {
            AddMethod(name, true);
        }

        if (py::hasattr(instance_, "Open")) {
            AddMethod("Open", false);
        }
        if (py::hasattr(instance_, "Close")) {
            AddMethod("Close", false);
        }
    }

    void AddMethod(const std::string& name, bool expose) {
        if (!py::hasattr(instance_, name.c_str())) {
            throw std::runtime_error("Python node missing method: " + name);
        }
        py::object callable = instance_.attr(name.c_str());
        if (!PyCallable_Check(callable.ptr())) {
            throw std::runtime_error("Python attribute is not callable: " + name);
        }
        size_t id = MethodIdForName(name);
        methods_[id] = GetSignatureMeta(callable, name);
        if (expose && name != "Open" && name != "Close") {
            exposed_methods_.push_back(name);
        }
    }

    PyMethodMeta GetSignatureMeta(const py::object& callable, const std::string& name) const {
        py::module_ inspect = py::module_::import("inspect");
        py::object signature = inspect.attr("signature")(callable);
        py::object params_obj = signature.attr("parameters");
        py::object values = params_obj.attr("values")();
        py::object param_type = inspect.attr("Parameter");
        py::object kind_positional = param_type.attr("POSITIONAL_OR_KEYWORD");
        py::object kind_pos_only = param_type.attr("POSITIONAL_ONLY");
        py::object kind_keyword_only = param_type.attr("KEYWORD_ONLY");
        py::object kind_var_pos = param_type.attr("VAR_POSITIONAL");
        py::object kind_var_kw = param_type.attr("VAR_KEYWORD");
        py::object empty = inspect.attr("_empty");

        bool first = true;
        PyMethodMeta meta;
        meta.name = name;
        for (const auto& param : values) {
            py::object kind = param.attr("kind");
            if (kind.is(kind_var_pos)) {
                meta.has_varargs = true;
                continue;
            }
            if (kind.is(kind_var_kw)) {
                meta.has_varkw = true;
                continue;
            }
            if (kind.is(kind_pos_only) || kind.is(kind_positional) || kind.is(kind_keyword_only)) {
                std::string param_name = py::cast<std::string>(param.attr("name"));
                if (first && param_name == "self") {
                    first = false;
                    continue;
                }
                meta.arg_names.push_back(param_name);
                py::object default_val = param.attr("default");
                if (!default_val.is(empty)) {
                    meta.defaults.push_back(default_val);
                    meta.has_default.push_back(true);
                } else {
                    meta.defaults.push_back(py::none());
                    meta.has_default.push_back(false);
                }
                first = false;
            }
        }
        meta.arg_count = meta.arg_names.size();
        return meta;
    }

    void ValidateSignature(const py::object& callable, const py::args& args, const py::kwargs& kwargs) const {
        py::module_ inspect = py::module_::import("inspect");
        py::object signature = inspect.attr("signature")(callable);
        py::object bind = signature.attr("bind");
        bind(*args, **kwargs);
    }


    size_t MethodIdForName(const std::string& name) const {
        if (name == "forward") {
            return easywork::ID_FORWARD;
        }
        if (name == "Open") {
            return easywork::ID_OPEN;
        }
        if (name == "Close") {
            return easywork::ID_CLOSE;
        }
        return easywork::hash_string(name);
    }

    easywork::Packet InvokeMethod(size_t method_id, const std::vector<easywork::Packet>& inputs) const {
        auto it = methods_.find(method_id);
        if (it == methods_.end()) {
            throw std::runtime_error("Method not found in Python node: " + std::to_string(method_id));
        }
        if (inputs.size() != it->second.arg_count) {
            throw std::runtime_error("Python node argument count mismatch for method: " + it->second.name);
        }
        py::gil_scoped_acquire gil;
        py::object callable = instance_.attr(it->second.name.c_str());
        py::tuple py_args(inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i) {
            py_args[i] = FromPacket(inputs[i]);
        }
        py::object result = callable(*py_args);
        if (result.is_none()) {
            return easywork::Packet::Empty();
        }
        return easywork::Packet::From(result, 0);
    }

    void PrepareInputConverters() {
        easywork::RegisterArithmeticConversions();
        port_converters_.clear();
        const auto py_object_type = easywork::TypeInfo::create<py::object>();

        for (const auto& [method_id, meta] : methods_) {
            for (size_t i = 0; i < meta.arg_count; ++i) {
                const easywork::TypeInfo& to_type = py_object_type;

                std::vector<easywork::Node*> potential_sources;
                if (mux_configs_[method_id].count(i)) {
                    const auto& cfg = mux_configs_[method_id][i];
                    for (const auto& pair : cfg.map) {
                        potential_sources.push_back(pair.second);
                    }
                } else {
                    for (const auto& u : upstreams_) {
                        int idx = FindPortIndex(u.node, method_id, i);
                        if (idx != -1) {
                            potential_sources.push_back(u.node);
                            break;
                        }
                    }
                }

                for (easywork::Node* src : potential_sources) {
                    int port_index = FindPortIndex(src, method_id, i);
                    if (port_index == -1) {
                        continue;
                    }

                    easywork::TypeInfo from_type = UpstreamOutputType(src);
                    if (from_type == to_type) {
                        continue;
                    }

                    if (*to_type.type_info == typeid(py::object)) {
                        continue;
                    }

                    if (!easywork::TypeConverterRegistry::instance().has_converter(*from_type.type_info, *to_type.type_info)) {
                        throw std::runtime_error(
                            "Type mismatch: cannot connect " + from_type.type_name +
                            " to " + to_type.type_name);
                    }

                    port_converters_[port_index] = [from_type, to_type](const easywork::Packet& packet) {
                        if (!packet.has_value()) {
                            return easywork::Packet::Empty();
                        }
                        std::any converted = easywork::TypeConverterRegistry::instance().convert(
                            packet.data(), *from_type.type_info, *to_type.type_info);
                        if (!converted.has_value()) {
                            throw std::runtime_error(
                                "Failed to convert " + from_type.type_name + " to " + to_type.type_name);
                        }
                        return easywork::Packet::FromAny(std::move(converted), packet.timestamp);
                    };
                }
            }
        }
    }

    easywork::TypeInfo UpstreamOutputType(easywork::Node* node) const {
        if (!node) {
            return easywork::TypeInfo::create<void>();
        }
        easywork::NodeTypeInfo info = node->get_type_info();
        auto it = info.methods.find(easywork::ID_FORWARD);
        if (it == info.methods.end()) {
            return easywork::TypeInfo::create<void>();
        }
        return it->second.output_type;
    }

    void RunDispatch() {
        try {
            EnsurePortBufferSize();
            BufferPortInputs();

            const auto& order = EffectiveMethodOrder();
            bool output_produced = false;

            for (size_t method_id : order) {
                auto it = methods_.find(method_id);
                if (it == methods_.end()) {
                    continue;
                }

                size_t required_args = it->second.arg_count;
                std::vector<easywork::Packet> inputs;
                inputs.reserve(required_args);

                bool method_ready = true;
                std::unordered_set<int> popped_controls;

                for (size_t i = 0; i < required_args; ++i) {
                    int selected_port = -1;
                    std::vector<int> discarded_ports;

                    if (mux_configs_[method_id].count(i)) {
                        const auto& cfg = mux_configs_[method_id][i];
                        int control_port = FindControlPortIndex(cfg.control_node);
                        if (control_port != -1 && !port_buffers_[control_port].empty()) {
                            easywork::Packet control_pkt = port_buffers_[control_port].front();

                            int choice = -1;
                            if (control_pkt.type() == easywork::TypeInfo::create<bool>()) {
                                choice = control_pkt.cast<bool>() ? 0 : 1;
                            } else if (control_pkt.type() == easywork::TypeInfo::create<int>()) {
                                choice = control_pkt.cast<int>();
                            } else if (control_pkt.type() == easywork::TypeInfo::create<int64_t>()) {
                                choice = static_cast<int>(control_pkt.cast<int64_t>());
                            } else if (control_pkt.type() == easywork::TypeInfo::create<py::object>()) {
                                py::gil_scoped_acquire gil;
                                py::object control_obj = FromPacket(control_pkt);
                                if (py::isinstance<py::bool_>(control_obj)) {
                                    choice = control_obj.cast<bool>() ? 0 : 1;
                                } else if (py::isinstance<py::int_>(control_obj)) {
                                    choice = control_obj.cast<int>();
                                } else {
                                    throw std::runtime_error("Mux control packet must be bool or int");
                                }
                            } else {
                                throw std::runtime_error("Mux control packet must be bool or int");
                            }

                            auto map_it = cfg.map.find(choice);
                            if (map_it != cfg.map.end()) {
                                selected_port = FindPortIndex(map_it->second, method_id, i);
                            } else {
                                throw std::runtime_error("Mux control value has no mapping");
                            }

                            for (const auto& pair : cfg.map) {
                                if (pair.first != choice) {
                                    int p_idx = FindPortIndex(pair.second, method_id, i);
                                    if (p_idx != -1) {
                                        discarded_ports.push_back(p_idx);
                                    }
                                }
                            }
                        }
                    } else {
                        for (size_t p = 0; p < port_map_.size(); ++p) {
                            if (port_map_[p].method_id == method_id &&
                                port_map_[p].arg_index == static_cast<int>(i)) {
                                selected_port = static_cast<int>(p);
                                break;
                            }
                        }
                    }

                    for (int p_idx : discarded_ports) {
                        if (p_idx >= 0 && p_idx < static_cast<int>(port_buffers_.size()) && !port_buffers_[p_idx].empty()) {
                            port_buffers_[p_idx].pop_front();
                        }
                    }

                    if (selected_port != -1 && selected_port < static_cast<int>(port_buffers_.size()) &&
                        !port_buffers_[selected_port].empty()) {
                        easywork::Packet pkt = port_buffers_[selected_port].front();
                        port_buffers_[selected_port].pop_front();

                        if (port_converters_.count(selected_port)) {
                            inputs.push_back(port_converters_[selected_port](pkt));
                        } else {
                            inputs.push_back(std::move(pkt));
                        }
                    } else {
                        method_ready = false;
                        break;
                    }
                }

                if (!method_ready) {
                    continue;
                }

                for (size_t i = 0; i < required_args; ++i) {
                    if (mux_configs_[method_id].count(i)) {
                        int c_idx = FindControlPortIndex(mux_configs_[method_id][i].control_node);
                        if (c_idx != -1 && !popped_controls.count(c_idx) && !port_buffers_[c_idx].empty()) {
                            port_buffers_[c_idx].pop_front();
                            popped_controls.insert(c_idx);
                        }
                    }
                }

                easywork::Packet result = InvokeMethod(method_id, inputs);

                if (result.has_value()) {
                    if (result.timestamp == 0) {
                        if (!inputs.empty()) {
                            result.timestamp = inputs[0].timestamp;
                        } else {
                            result.timestamp = easywork::Packet::NowNs();
                        }
                    }
                    output_packet_ = std::move(result);
                    output_produced = true;
                }
            }

            if (!output_produced) {
                output_packet_ = easywork::Packet::Empty();
            }
        } catch (const std::exception& e) {
            std::cerr << "Python Dispatch Error: " << e.what() << std::endl;
            output_packet_ = easywork::Packet::Empty();
        }
    }

    std::unordered_map<size_t, PyMethodMeta> methods_;
    std::vector<std::string> exposed_methods_;
    std::string type_name_;
    py::object instance_;
};

} // namespace

PYBIND11_MODULE(easywork_core, m) {
    m.doc() = "EasyWork Core (Taskflow + OpenCV) with C++20 Factory Pattern + Type System";

    RegisterPythonConverters();
    RegisterArithmeticConverters();

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
            node.Open(BuildInputsForMethod(node, easywork::ID_OPEN, "Open", args, false));
        })
        .def("close", [](easywork::Node& node, py::args args, py::kwargs kwargs) {
            if (kwargs && kwargs.size() > 0) throw std::runtime_error("Kwargs not supported in Node.close()");
            node.Close(BuildInputsForMethod(node, easywork::ID_CLOSE, "Close", args, false));
        })
        .def("is_open", &easywork::Node::IsOpen)
        .def("invoke", [](easywork::Node& node, const std::string& method, py::args args, py::kwargs kwargs) {
            size_t method_id = method == "forward" ? easywork::ID_FORWARD : easywork::hash_string(method);
            if (kwargs && kwargs.size() > 0) {
                auto* py_node = dynamic_cast<PyNode*>(&node);
                if (!py_node) {
                    throw std::runtime_error("Kwargs are only supported for Python nodes");
                }
                easywork::Packet result = py_node->invoke_python(method_id, args, kwargs);
                if (!result.has_value()) {
                    return py::object(py::none());
                }
                return FromPacket(result);
            }

            std::vector<easywork::Packet> inputs =
                BuildInputsForMethod(node, method_id, method, args, true);
            easywork::Packet result = node.invoke(method_id, inputs);

            if (!result.has_value()) {
                return py::object(py::none());
            }
            return FromPacket(result);
        })
        .def("set_input", &easywork::Node::set_input, py::arg("upstream"), py::arg("arg_idx") = -1)
        .def("set_weak_input", &easywork::Node::set_weak_input, py::arg("upstream"), py::arg("arg_idx") = -1)
        .def("set_input_for", &easywork::Node::set_input_for, py::arg("method"), py::arg("upstream"), py::arg("arg_idx") = -1)
        .def("set_input_mux", &easywork::Node::SetInputMux, py::arg("method"), py::arg("arg_idx"), py::arg("control"), py::arg("map"))
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
        })
        .def_property_readonly("is_python_node", [](const easywork::Node& self) {
            return dynamic_cast<const PyNode*>(&self) != nullptr;
        })
        .def("get_method_arg_names", [](easywork::Node& node, const std::string& method) {
            auto* py_node = dynamic_cast<PyNode*>(&node);
            if (!py_node) {
                return std::vector<std::string>{};
            }
            size_t method_id = method == "forward" ? easywork::ID_FORWARD : easywork::hash_string(method);
            return py_node->GetMethodArgNames(method_id);
        })
        .def("method_has_varargs", [](easywork::Node& node, const std::string& method) {
            auto* py_node = dynamic_cast<PyNode*>(&node);
            if (!py_node) {
                return false;
            }
            size_t method_id = method == "forward" ? easywork::ID_FORWARD : easywork::hash_string(method);
            return py_node->MethodHasVarArgs(method_id);
        })
        .def("get_method_defaults", [](easywork::Node& node, const std::string& method) {
            auto* py_node = dynamic_cast<PyNode*>(&node);
            if (!py_node) {
                return std::unordered_map<std::string, py::object>{};
            }
            size_t method_id = method == "forward" ? easywork::ID_FORWARD : easywork::hash_string(method);
            return py_node->GetMethodDefaults(method_id);
        });

    py::class_<easywork::IfNode, easywork::Node, std::shared_ptr<easywork::IfNode>>(m, "IfNode")
        .def("add_true_branch", &easywork::IfNode::AddTrueBranch)
        .def("add_false_branch", &easywork::IfNode::AddFalseBranch);

    // ========== NodeRegistry ==========

    py::class_<easywork::NodeRegistry>(m, "_NodeRegistry")
        .def_static("instance", &easywork::NodeRegistry::instance, py::return_value_policy::reference)
        .def("registered_nodes", &easywork::NodeRegistry::RegisteredNodes)
        .def("is_registered", &easywork::NodeRegistry::IsRegistered);

    m.def("create_node", [](const std::string& name, py::args args, py::kwargs kwargs) {
        return easywork::NodeRegistry::instance().Create(name, args, kwargs);
    });

    m.def("create_node_from_instance", [](py::object instance) -> std::shared_ptr<easywork::Node> {
        py::gil_scoped_acquire gil;
        return std::make_shared<PyNode>(instance);
    });

    m.def("register_python_node", [](const std::string& name, py::object py_class) {
        easywork::NodeRegistry::instance().Register(
            name,
            [py_class](py::args args, py::kwargs kwargs) -> std::shared_ptr<easywork::Node> {
                py::gil_scoped_acquire gil;
                py::object instance = py_class(*args, **kwargs);
                return std::make_shared<PyNode>(instance);
            });
    });

    // ========== Tuple Helpers ==========

    m.def("create_tuple_get_node", &easywork::CreateTupleGetNode);
    m.def("get_tuple_size", &easywork::GetTupleSize);
    m.def("register_arithmetic_conversions", &RegisterArithmeticConverters);
    m.def("can_convert", &CanConvertTypes);
    
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
