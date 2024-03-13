/*
Copyright 2020 Lucas Heitzmann Gabrielli.
This file is part of gdstk, distributed under the terms of the
Boost Software License - Version 1.0.  See the accompanying
LICENSE file or <http://www.boost.org/LICENSE_1_0.txt>
*/
#include <iostream>
static PyObject* rawcell_object_str(RawCellObject* self) {
    char buffer[GDSTK_PRINT_BUFFER_COUNT];
    snprintf(buffer, COUNT(buffer),
             "RawCell '%s' with %" PRIu64 " bytes and %" PRIu64 " dependencies",
             self->rawcell->name, self->rawcell->size, self->rawcell->dependencies.count);
    return PyUnicode_FromString(buffer);
}

static void rawcell_object_dealloc(RawCellObject* self) {
    RawCell* rawcell = self->rawcell;
    if (rawcell) {
        for (uint64_t i = 0; i < rawcell->dependencies.count; i++) {
            Py_XDECREF(rawcell->dependencies[i]->owner);
        }
        rawcell->clear();
        free_allocation(rawcell);
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static int rawcell_object_init(RawCellObject* self, PyObject* args, PyObject* kwds) {
    const char* keywords[] = {"name", NULL};
    char* name = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s:RawCell", (char**)keywords, &name)) return -1;
    RawCell* rawcell = self->rawcell;
    if (rawcell) {
        rawcell->clear();
    } else {
        self->rawcell = (RawCell*)allocate_clear(sizeof(RawCell));
        rawcell = self->rawcell;
    }
    uint64_t len;
    rawcell->name = copy_string(name, &len);
    rawcell->owner = self;
    if (len <= 1) {
        PyErr_SetString(PyExc_ValueError, "Empty cell name.");
        return -1;
    }
    return 0;
}

static PyObject* rawcell_object_dependencies(RawCellObject* self, PyObject* args) {
    int recursive;
    if (!PyArg_ParseTuple(args, "p:dependencies", &recursive)) return NULL;
    Map<RawCell*> rawcell_map = {};
    self->rawcell->get_dependencies(recursive > 0, rawcell_map);
    PyObject* result = PyList_New(rawcell_map.count);
    if (!result) {
        PyErr_SetString(PyExc_RuntimeError, "Unable to create list.");
        rawcell_map.clear();
        return NULL;
    }
    uint64_t i = 0;
    for (MapItem<RawCell*>* item = rawcell_map.next(NULL); item != NULL;
         item = rawcell_map.next(item)) {
        PyObject* rawcell_obj = (PyObject*)item->value->owner;
        Py_INCREF(rawcell_obj);
        PyList_SET_ITEM(result, i++, rawcell_obj);
    }
    rawcell_map.clear();
    return result;
}

static PyObject* rawcell_object_get_polygons(RawCellObject* self, PyObject* args) {
    double unit = 0;
    double tolerance = 0;
    int64_t depth = 0;
    int start_id = 0;
    PyObject* error_code_obj = NULL;

    // Parse the arguments
    if (!PyArg_ParseTuple(args, "|dddO", &depth, &unit, &tolerance, &error_code_obj))
        return NULL;

    // Convert error_code_obj to C++ ErrorCode* if provided
    ErrorCode* error_code = NULL;
    if (error_code_obj != NULL) {
        // Implementation to convert Python object to C++ ErrorCode* goes here
    }

    // Vector to store polygons
    std::vector<std::vector<int>> polygons;
    // Call the get_polygons method
    self->rawcell->get_polygons(start_id, depth, unit, tolerance, error_code, polygons);

    // Create a NumPy array for storing the polygons
    npy_intp dims[] = {static_cast<npy_intp>(polygons.size()), 3}; // 3 columns: x, y, poly_id
    PyObject* numpy_array = PyArray_SimpleNew(2, dims, NPY_INT32);
    if (numpy_array == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create NumPy array.");
        return NULL;
    }

    // Get a pointer to the data in the NumPy array
    int32_t* numpy_data = reinterpret_cast<int32_t*>(PyArray_DATA(reinterpret_cast<PyArrayObject*>(numpy_array)));

    // Copy the polygon data into the NumPy array
    for (size_t i = 0; i < polygons.size(); i++) {
        for (size_t j = 0; j < polygons[i].size(); j++) {
            numpy_data[i * 3 + j] = polygons[i][j];
        }
    }

    // Return the NumPy array
    return numpy_array;
}


static PyMethodDef rawcell_object_methods[] = {
    {"dependencies", (PyCFunction)rawcell_object_dependencies, METH_VARARGS,
     rawcell_object_dependencies_doc},
    {"get_polygons", (PyCFunction)rawcell_object_get_polygons, METH_VARARGS,
     rawcell_object_get_polygons_doc},
    {NULL}};

PyObject* rawcell_object_get_name(RawCellObject* self, void*) {
    PyObject* result = PyUnicode_FromString(self->rawcell->name);
    if (!result) {
        PyErr_SetString(PyExc_TypeError, "Unable to convert value to string.");
        return NULL;
    }
    return result;
}

PyObject* rawcell_object_get_size(RawCellObject* self, void*) {
    PyObject* result = PyLong_FromUnsignedLongLong(self->rawcell->size);
    if (!result) {
        PyErr_SetString(PyExc_TypeError, "Unable to convert value to long.");
        return NULL;
    }
    return result;
}

PyObject* rawcell_object_get_filename(RawCellObject* self, void*) {
    const char * c = self->rawcell->filename.c_str();

    PyObject* result = PyUnicode_FromString(c);
    if (!result) {
        PyErr_SetString(PyExc_TypeError, "Unable to convert value to string.");
        return NULL;
    }
    return result;
}

static PyGetSetDef rawcell_object_getset[] = {
    {"filename", (getter)rawcell_object_get_filename, NULL, rawcell_object_filename_doc, NULL},
    {"name", (getter)rawcell_object_get_name, NULL, rawcell_object_name_doc, NULL},
    {"size", (getter)rawcell_object_get_size, NULL, rawcell_object_size_doc, NULL},
    {NULL}};
