/** Copyright 2020-2021 Alibaba Group Holding Limited.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "pybind11_utils.h"  // NOLINT(build/include_subdir)

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

#include "common/util/json.h"

#include "pybind11/pybind11.h"

namespace py = pybind11;
using namespace py::literals;  // NOLINT(build/namespaces_literals)

namespace vineyard {

static PyObject* vineyard_add_doc(PyObject* /* unused */, PyObject* args) {
  PyObject* obj;
  PyObject* doc_obj;
  if (!PyArg_ParseTuple(args, "OO", &obj, &doc_obj)) {
    return PyErr_Format(PyExc_RuntimeError,
                        "Two arguments (value, doc) is required");
  }

  // adds a __doc__ string to a function, similar to pytorch's _add_docstr
  const char* doc_str = "<invalid string>";
  Py_ssize_t doc_len = std::strlen(doc_str);
  if (PyBytes_Check(doc_obj)) {
    doc_len = PyBytes_GET_SIZE(doc_obj);
    doc_str = PyBytes_AS_STRING(doc_obj);
  }
  if (PyUnicode_Check(doc_obj)) {
    doc_str = PyUnicode_AsUTF8AndSize(doc_obj, &doc_len);
    if (!doc_str) {
      return PyErr_Format(PyExc_RuntimeError,
                          "error unpacking string as utf-8");
    }
  }

  if (Py_TYPE(obj) == &PyCFunction_Type) {
    PyCFunctionObject* f =
        (PyCFunctionObject*) obj;  // NOLINT(readability/casting)
    if (f->m_ml->ml_doc && f->m_ml->ml_doc[0]) {
      return PyErr_Format(PyExc_RuntimeError,
                          "function '%s' already has a docstring",
                          f->m_ml->ml_name);
    }
    // copy the doc string since pybind11 will release the doc unconditionally.
    f->m_ml->ml_doc = strndup(doc_str, doc_len);
  } else if (Py_TYPE(obj) == &PyInstanceMethod_Type) {
    PyObject* fobj = PyInstanceMethod_GET_FUNCTION(obj);
    PyCFunctionObject* f =
        (PyCFunctionObject*) fobj;  // NOLINT(readability/casting)
    if (f->m_ml->ml_doc && f->m_ml->ml_doc[0]) {
      return PyErr_Format(PyExc_RuntimeError,
                          "function '%s' already has a docstring",
                          f->m_ml->ml_name);
    }
    // copy the doc string since pybind11 will release the doc unconditionally.
    f->m_ml->ml_doc = strndup(doc_str, doc_len);
  } else if (strcmp(Py_TYPE(obj)->tp_name, "method_descriptor") == 0) {
    PyMethodDescrObject* m =
        (PyMethodDescrObject*) obj;  // NOLINT(readability/casting)
    if (m->d_method->ml_doc && m->d_method->ml_doc[0]) {
      return PyErr_Format(PyExc_RuntimeError,
                          "method '%s' already has a docstring",
                          m->d_method->ml_name);
    }
    // copy the doc string since pybind11 will release the doc unconditionally.
    m->d_method->ml_doc = strndup(doc_str, doc_len);
  } else if (strcmp(Py_TYPE(obj)->tp_name, "getset_descriptor") == 0) {
    PyGetSetDescrObject* m =
        (PyGetSetDescrObject*) obj;  // NOLINT(readability/casting)
    if (m->d_getset->doc && m->d_getset->doc[0]) {
      return PyErr_Format(PyExc_RuntimeError,
                          "attribute '%s' already has a docstring",
                          m->d_getset->name);
    }
    // This field is not const for python < 3.7 yet the content is
    // never modified.
    // copy the doc string since pybind11 will release the doc unconditionally.
    m->d_getset->doc = const_cast<char*>(strndup(doc_str, doc_len));
  } else if (Py_TYPE(obj) == &PyType_Type) {
    PyTypeObject* t = (PyTypeObject*) obj;  // NOLINT(readability/casting)
    if (t->tp_doc && t->tp_doc[0]) {
      return PyErr_Format(PyExc_RuntimeError,
                          "Type '%s' already has a docstring", t->tp_name);
    }
    // copy the doc string since pybind11 will release the doc unconditionally.
    t->tp_doc = strndup(doc_str, doc_len);
  } else {
    PyObject* doc_attr = nullptr;
    doc_attr = PyObject_GetAttrString(obj, "__doc__");
    if (doc_attr != NULL && doc_attr != Py_None &&
        ((PyBytes_Check(doc_attr) && PyBytes_GET_SIZE(doc_attr) > 0) ||
         (PyUnicode_Check(doc_attr) && PyUnicode_GET_LENGTH(doc_attr) > 0))) {
      return PyErr_Format(PyExc_RuntimeError, "Object already has a docstring");
    }
    Py_XDECREF(doc_attr);
    Py_XINCREF(doc_obj);
    if (PyObject_SetAttrString(obj, "__doc__", doc_obj) < 0) {
      return PyErr_Format(PyExc_TypeError,
                          "Cannot set a docstring for that object");
    }
  }
  Py_RETURN_NONE;
}

static PyMethodDef vineyard_utils_methods[] = {
    {"_add_doc", vineyard_add_doc, METH_VARARGS,
     "Associate docstring with pybind11 exposed functions, types and methods."},
    {NULL, NULL, 0, NULL},
};

void bind_utils(py::module& mod) {
  PyModule_AddFunctions(mod.ptr(), vineyard_utils_methods);
}

namespace detail {

/**
 * Cast nlohmann::json to python object.
 *
 * Refer to https://github.com/pybind/pybind11_json
 */
py::object from_json(const json& j) {
  if (j.is_null()) {
    return py::none();
  } else if (j.is_boolean()) {
    return py::bool_(j.get<bool>());
  } else if (j.is_number_integer()) {
    return py::int_(j.get<json::number_integer_t>());
  } else if (j.is_number_unsigned()) {
    return py::int_(j.get<json::number_unsigned_t>());
  } else if (j.is_number_float()) {
    return py::float_(j.get<double>());
  } else if (j.is_string()) {
    return py::str(j.get<std::string>());
  } else if (j.is_array()) {
    py::list obj;
    for (const auto& el : j) {
      obj.append(from_json(el));
    }
    return std::move(obj);
  } else {
    // Object
    py::dict obj;
    for (json::const_iterator it = j.cbegin(); it != j.cend(); ++it) {
      obj[py::str(it.key())] = from_json(it.value());
    }
    return std::move(obj);
  }
}

/**
 * Cast python object to nlohmann::json.
 *
 * Refer to https://github.com/pybind/pybind11_json
 */
json to_json(const py::handle& obj) {
  if (obj.ptr() == nullptr || obj.is_none()) {
    return nullptr;
  }
  if (py::isinstance<py::bool_>(obj)) {
    return obj.cast<bool>();
  }
  if (py::isinstance<py::int_>(obj)) {
    try {
      json::number_integer_t s = obj.cast<json::number_integer_t>();
      if (py::int_(s).equal(obj)) {
        return s;
      }
    } catch (...) {}
    try {
      json::number_unsigned_t u = obj.cast<json::number_unsigned_t>();
      if (py::int_(u).equal(obj)) {
        return u;
      }
    } catch (...) {}
    throw std::runtime_error(
        "to_json received an integer out of range for both "
        "json::number_integer_t and json::number_unsigned_t type: " +
        py::repr(obj).cast<std::string>());
  }
  if (py::isinstance<py::float_>(obj)) {
    return obj.cast<double>();
  }
  if (py::isinstance<py::bytes>(obj)) {
    py::module base64 = py::module::import("base64");
    return base64.attr("b64encode")(obj)
        .attr("decode")("utf-8")
        .cast<std::string>();
  }
  if (py::isinstance<py::str>(obj)) {
    return obj.cast<std::string>();
  }
  if (py::isinstance<py::tuple>(obj) || py::isinstance<py::list>(obj)) {
    auto out = json::array();
    for (const py::handle value : obj) {
      out.push_back(to_json(value));
    }
    return out;
  }
  if (py::isinstance<py::dict>(obj)) {
    auto out = json::object();
    for (const py::handle key : obj) {
      out[py::str(key).cast<std::string>()] = to_json(obj[key]);
    }
    return out;
  }
  throw std::runtime_error("to_json not implemented for this type of object: " +
                           py::repr(obj).cast<std::string>());
}

}  // namespace detail

}  // namespace vineyard
