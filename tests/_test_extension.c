/* This is a set of functions used by test_extension_interface.py to test the
 * Greenstack C API.
 */

#include "../greenstack.h"

#ifndef Py_RETURN_NONE
#define Py_RETURN_NONE return Py_INCREF(Py_None), Py_None
#endif

#define TEST_MODULE_NAME "_test_extension"

static PyObject *
test_switch(PyObject *self, PyObject *greenstack)
{
	PyObject *result = NULL;

	if (greenstack == NULL || !PyGreenstack_Check(greenstack)) {
		PyErr_BadArgument();
		return NULL;
	}

	result = PyGreenstack_Switch((PyGreenstack *) greenstack, NULL, NULL);
	if (result == NULL) {
		if (!PyErr_Occurred()) {
			PyErr_SetString(
				PyExc_AssertionError,
				"greenstack.switch() failed for some reason.");
		}
		return NULL;
	}
	Py_INCREF(result);
	return result;
}

static PyObject *
test_switch_kwargs(PyObject *self, PyObject *args, PyObject *kwargs)
{
	PyGreenstack *g = NULL;
	PyObject *result = NULL;

	PyArg_ParseTuple(args, "O!", &PyGreenstack_Type, &g);

	if (g == NULL || !PyGreenstack_Check(g)) {
		PyErr_BadArgument();
		return NULL;
	}

	result = PyGreenstack_Switch(g, NULL, kwargs);
	if (result == NULL) {
		if (!PyErr_Occurred()) {
			PyErr_SetString(
				PyExc_AssertionError,
				"greenstack.switch() failed for some reason.");
		}
		return NULL;
	}
	Py_XINCREF(result);
	return result;
}

static PyObject *
test_getcurrent(PyObject *self)
{
	PyGreenstack *g = PyGreenstack_GetCurrent();
	if (g == NULL || !PyGreenstack_Check(g) || !PyGreenstack_ACTIVE(g)) {
		PyErr_SetString(
			PyExc_AssertionError,
			"getcurrent() returned an invalid greenstack");
		Py_XDECREF(g);
		return NULL;
	}
	Py_DECREF(g);
	Py_RETURN_NONE;
}

static PyObject *
test_setparent(PyObject *self, PyObject *arg)
{
	PyGreenstack *current;
	PyGreenstack *greenstack = NULL;

	if (arg == NULL || !PyGreenstack_Check(arg))
	{
		PyErr_BadArgument();
		return NULL;
	}
	if ((current = PyGreenstack_GetCurrent()) == NULL) {
		return NULL;
	}
	greenstack = (PyGreenstack *) arg;
	if (PyGreenstack_SetParent(greenstack, current)) {
		Py_DECREF(current);
		return NULL;
	}
	Py_DECREF(current);
	if (PyGreenstack_Switch(greenstack, NULL, NULL) == NULL) {
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject *
test_new_greenstack(PyObject *self, PyObject *callable)
{
	PyObject *result = NULL;
	PyGreenstack *greenstack = PyGreenstack_New(callable, NULL);

	if (!greenstack) {
		return NULL;
	}

	result = PyGreenstack_Switch(greenstack, NULL, NULL);
	if (result == NULL) {
		return NULL;
	}

	Py_INCREF(result);
	return result;
}

static PyObject *
test_raise_dead_greenstack(PyObject *self)
{
	PyErr_SetString(PyExc_GreenstackExit, "test GreenstackExit exception.");
	return NULL;
}

static PyObject *
test_raise_greenstack_error(PyObject *self)
{
	PyErr_SetString(PyExc_GreenstackError, "test greenstack.error exception");
	return NULL;
}

static PyObject *
test_throw(PyObject *self, PyGreenstack *g)
{
	const char msg[] = "take that sucka!";
	PyObject *msg_obj = Py_BuildValue("s", msg);
	PyGreenstack_Throw(g, PyExc_ValueError, msg_obj, NULL);
	Py_DECREF(msg_obj);
	Py_RETURN_NONE;
}

static PyMethodDef test_methods[] = {
	{"test_switch", (PyCFunction) test_switch, METH_O,
	 "Switch to the provided greenstack sending provided arguments, and \n"
	 "return the results."},
	{"test_switch_kwargs", (PyCFunction) test_switch_kwargs,
	 METH_VARARGS | METH_KEYWORDS,
	 "Switch to the provided greenstack sending the provided keyword args."},
	{"test_getcurrent", (PyCFunction) test_getcurrent, METH_NOARGS,
	 "Test PyGreenstack_GetCurrent()"},
	{"test_setparent", (PyCFunction) test_setparent, METH_O,
	 "Se the parent of the provided greenstack and switch to it."},
	{"test_new_greenstack", (PyCFunction) test_new_greenstack, METH_O,
	 "Test PyGreenstack_New()"},
	{"test_raise_dead_greenstack", (PyCFunction) test_raise_dead_greenstack,
	 METH_NOARGS, "Just raise greenstack.GreenstackExit"},
	{"test_raise_greenstack_error", (PyCFunction) test_raise_greenstack_error,
	 METH_NOARGS, "Just raise greenstack.error"},
	{"test_throw", (PyCFunction) test_throw, METH_O,
	 "Throw a ValueError at the provided greenstack"},
	{NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3
#define INITERROR return NULL

static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	TEST_MODULE_NAME,
	NULL,
	0,
	test_methods,
	NULL,
	NULL,
	NULL,
	NULL
};

PyMODINIT_FUNC
PyInit__test_extension(void)
#else
#define INITERROR return
PyMODINIT_FUNC
init_test_extension(void)
#endif
{
	PyObject *module = NULL;

#if PY_MAJOR_VERSION >= 3
	module = PyModule_Create(&moduledef);
#else
	module = Py_InitModule(TEST_MODULE_NAME, test_methods);
#endif

	if (module == NULL) {
		INITERROR;
	}

	PyGreenstack_Import();

#if PY_MAJOR_VERSION >= 3
	return module;
#endif
}
