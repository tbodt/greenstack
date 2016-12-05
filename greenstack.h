/* vim:set noet ts=8 sw=8 : */

/* Stack greenlet object interface */

#ifndef Py_GREENSTACK_H
#define Py_GREENSTACK_H

#include <Python.h>

#ifdef GREENSTACK_MODULE
#include "libcoro/coro.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define GREENSTACK_VERSION "0.4.10"

typedef struct _greenstack {
	PyObject_HEAD
	void *stack;
	size_t stack_size;
	struct _greenstack* parent;
	PyObject* run_info;
	struct _frame* top_frame;
	PyObject* weakreflist;
	PyObject* dict;
#ifdef GREENSTACK_MODULE
	/* This field is hidden from the C API because its contents are
	 * platform-dependent and require a bunch of macros to be defined,
	 * which I don't want to make anyone do. */
	coro_context context;
#endif
} PyStackGreenlet;

#define PyStackGreenlet_Check(op)      PyObject_TypeCheck(op, &PyStackGreenlet_Type)
#define PyStackGreenlet_MAIN(op)       (((PyStackGreenlet*)(op))->stack == (char *) 1)
#define PyStackGreenlet_STARTED(op)    (((PyStackGreenlet*)(op))->stack_size != 0)
#define PyStackGreenlet_ACTIVE(op)     (((PyStackGreenlet*)(op))->stack != NULL)
#define PyStackGreenlet_GET_PARENT(op) (((PyStackGreenlet*)(op))->parent)

#if (PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 7) || (PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION >= 1) || PY_MAJOR_VERSION > 3
#define GREENSTACK_USE_PYCAPSULE
#endif

/* C API functions */

/* Total number of symbols that are exported */
#define PyStackGreenlet_API_pointers  8

#define PyStackGreenlet_Type_NUM       0
#define PyExc_StackGreenletError_NUM   1
#define PyExc_StackGreenletExit_NUM    2

#define PyStackGreenlet_New_NUM        3
#define PyStackGreenlet_GetCurrent_NUM 4
#define PyStackGreenlet_Throw_NUM      5
#define PyStackGreenlet_Switch_NUM     6
#define PyStackGreenlet_SetParent_NUM  7

#ifndef GREENSTACK_MODULE
/* This section is used by modules that uses the greenstack C API */
static void **_PyStackGreenlet_API = NULL;

#define PyStackGreenlet_Type (*(PyTypeObject *) _PyStackGreenlet_API[PyStackGreenlet_Type_NUM])

#define PyExc_StackGreenletError \
	((PyObject *) _PyStackGreenlet_API[PyExc_StackGreenletError_NUM])

#define PyExc_StackGreenletExit \
	((PyObject *) _PyStackGreenlet_API[PyExc_StackGreenletExit_NUM])

/*
 * PyStackGreenlet_New(PyObject *args)
 *
 * greenstack.greenlet(run, parent=None)
 */
#define PyStackGreenlet_New \
	(* (PyStackGreenlet * (*)(PyObject *run, PyStackGreenlet *parent)) \
	 _PyStackGreenlet_API[PyStackGreenlet_New_NUM])

/*
 * PyStackGreenlet_GetCurrent(void)
 *
 * greenstack.getcurrent()
 */
#define PyStackGreenlet_GetCurrent \
	(* (PyStackGreenlet * (*)(void)) _PyStackGreenlet_API[PyStackGreenlet_GetCurrent_NUM])

/*
 * PyStackGreenlet_Throw(
 *         PyStackGreenlet *greenlet,
 *         PyObject *typ,
 *         PyObject *val,
 *         PyObject *tb)
 *
 * g.throw(...)
 */
#define PyStackGreenlet_Throw \
	(* (PyObject * (*) \
	    (PyStackGreenlet *self, PyObject *typ, PyObject *val, PyObject *tb)) \
	 _PyStackGreenlet_API[PyStackGreenlet_Throw_NUM])

/*
 * PyStackGreenlet_Switch(PyStackGreenlet *greenlet, PyObject *args)
 *
 * g.switch(*args, **kwargs)
 */
#define PyStackGreenlet_Switch \
	(* (PyObject * (*)(PyStackGreenlet *greenlet, PyObject *args, PyObject *kwargs)) \
	 _PyStackGreenlet_API[PyStackGreenlet_Switch_NUM])

/*
 * PyStackGreenlet_SetParent(PyObject *greenlet, PyObject *new_parent)
 *
 * g.parent = new_parent
 */
#define PyStackGreenlet_SetParent \
	(* (int (*)(PyStackGreenlet *greenlet, PyStackGreenlet *nparent)) \
	 _PyStackGreenlet_API[PyStackGreenlet_SetParent_NUM])

/* Macro that imports greenstack and initializes C API */
#ifdef GREENSTACK_USE_PYCAPSULE
#define PyStackGreenlet_Import() \
{ \
	_PyStackGreenlet_API = (void**)PyCapsule_Import("greenstack._C_API", 0); \
}
#else
#define PyStackGreenlet_Import() \
{ \
	PyObject *module = PyImport_ImportModule("greenstack"); \
	if (module != NULL) { \
		PyObject *c_api_object = PyObject_GetAttrString( \
			module, "_C_API"); \
		if (c_api_object != NULL && PyCObject_Check(c_api_object)) { \
			_PyStackGreenlet_API = \
				(void **) PyCObject_AsVoidPtr(c_api_object); \
			Py_DECREF(c_api_object); \
		} \
		Py_DECREF(module); \
	} \
}
#endif

#endif /* GREENSTACK_MODULE */

#ifdef __cplusplus
}
#endif
#endif /* !Py_GREENSTACK_H */
