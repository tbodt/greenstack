/* vim:set noet ts=8 sw=8 : */

#define GREENSTACK_MODULE

/* explaination of everything would go here but i'm probably just going to delete this */

#include "greenstack.h"
#include "structmember.h"

/* Python <= 2.5 support */
#if PY_MAJOR_VERSION < 3
#ifndef Py_REFCNT
#  define Py_REFCNT(ob) (((PyObject *) (ob))->ob_refcnt)
#endif
#ifndef Py_TYPE
#  define Py_TYPE(ob)   (((PyObject *) (ob))->ob_type)
#endif
#ifndef PyVarObject_HEAD_INIT
#  define PyVarObject_HEAD_INIT(type, size) \
	PyObject_HEAD_INIT(type) size,
#endif
#endif

#if PY_VERSION_HEX < 0x02060000
#define PyLong_FromSsize_t PyInt_FromLong
#endif

#if PY_VERSION_HEX < 0x02050000
typedef int Py_ssize_t;
#endif

extern PyTypeObject PyGreenstack_Type;

/* Defines that customize greenstack module behaviour */
#ifndef GREENSTACK_USE_GC
#define GREENSTACK_USE_GC 1
#endif

#ifndef GREENSTACK_USE_TRACING
#define GREENSTACK_USE_TRACING 1
#endif

/*** global state ***/

/* In the presence of multithreading, this is a bit tricky:

   - ts_current always store a reference to a greenstack, but it is
   not really the current greenstack after a thread switch occurred.

   - each *running* greenstack uses its run_info field to know which
   thread it is attached to.  A greenstack can only run in the thread
     where it was created.  This run_info is a ref to tstate->dict.

   - the thread state dict is used to save and restore ts_current,
     using the dictionary key 'ts_curkey'.
*/

/* Weak reference to the switching-to greenstack during the slp switch */
static PyGreenstack* volatile ts_target = NULL;
/* Strong reference to the switching from greenstack after the switch */
static PyGreenstack* volatile ts_origin = NULL;
/* Strong reference to the current greenstack in this thread state */
static PyGreenstack* volatile ts_current = NULL;
/* NULL if error, otherwise args tuple to pass around during coro switch */
static PyObject* volatile ts_passaround_args = NULL;
static PyObject* volatile ts_passaround_kwargs = NULL;

/***********************************************************/
/* Thread-aware routines, switching global variables when needed */

#define STATE_OK    (ts_current->run_info == PyThreadState_GET()->dict \
                     || !green_updatecurrent())

static PyObject* ts_curkey;
static PyObject* ts_delkey;
#if GREENSTACK_USE_TRACING
static PyObject* ts_tracekey;
static PyObject* ts_event_switch;
static PyObject* ts_event_throw;
#endif
static PyObject* PyExc_GreenstackError;
static PyObject* PyExc_GreenstackExit;
static PyObject* ts_empty_tuple;
static PyObject* ts_empty_dict;

/* 
 * To reduce the cost of allocating and destroying stacks for greenstacks,
 * greenstack stacks are never destroyed. Instead they are saved in a stack (a
 * stack of stacks, if you will) and reused for future greenstacks. 
 */

#define STACK_CACHE_SIZE 8192
#define STACK_CACHE_FULL (stack_cache_top >= STACK_CACHE_SIZE)
static struct coro_stack stack_cache[STACK_CACHE_SIZE];
static int stack_cache_top;

/* State handlers are used by C extensions to save and restore custom state.
 * Switch wrappers are called by g_switch and state initializers are called
 * from g_trampoline. */

typedef struct _statehandler statehandler;
static void g_realswitchstack();
static statehandler main_statehandler = {
	g_realswitchstack, /* switchwrapper */
	NULL,              /* stateinit */
	NULL
};
static statehandler *statehandlers = &main_statehandler;

#if GREENSTACK_USE_GC
#define GREENSTACK_GC_FLAGS Py_TPFLAGS_HAVE_GC
#define GREENSTACK_tp_alloc PyType_GenericAlloc
#define GREENSTACK_tp_free PyObject_GC_Del
#define GREENSTACK_tp_traverse green_traverse
#define GREENSTACK_tp_clear green_clear
#define GREENSTACK_tp_is_gc green_is_gc
#else /* GREENSTACK_USE_GC */
#define GREENSTACK_GC_FLAGS 0
#define GREENSTACK_tp_alloc 0
#define GREENSTACK_tp_free 0
#define GREENSTACK_tp_traverse 0
#define GREENSTACK_tp_clear 0
#define GREENSTACK_tp_is_gc 0
#endif /* !GREENSTACK_USE_GC */

static PyGreenstack* green_create_main(void)
{
	PyGreenstack* gmain;
	PyObject* dict = PyThreadState_GetDict();
	if (dict == NULL) {
		if (!PyErr_Occurred())
			PyErr_NoMemory();
		return NULL;
	}

	/* create the main greenstack for this thread */
	gmain = (PyGreenstack*) PyType_GenericAlloc(&PyGreenstack_Type, 0);
	if (gmain == NULL)
		return NULL;
	coro_create(&gmain->context, NULL, NULL, NULL, 0);
	gmain->stack = (void *) 1;
	gmain->stack_size = (size_t) -1;
	gmain->run_info = dict;
	Py_INCREF(dict);
	return gmain;
}

static int green_updatecurrent(void)
{
	PyObject *exc, *val, *tb;
	PyThreadState* tstate;
	PyGreenstack* current;
	PyGreenstack* previous;
	PyObject* deleteme;

green_updatecurrent_restart:
	/* save current exception */
	PyErr_Fetch(&exc, &val, &tb);

	/* get ts_current from the active tstate */
	tstate = PyThreadState_GET();
	if (tstate->dict && (current =
	    (PyGreenstack*) PyDict_GetItem(tstate->dict, ts_curkey))) {
		/* found -- remove it, to avoid keeping a ref */
		Py_INCREF(current);
		PyDict_DelItem(tstate->dict, ts_curkey);
	}
	else {
		/* first time we see this tstate */
		current = green_create_main();
		if (current == NULL) {
			Py_XDECREF(exc);
			Py_XDECREF(val);
			Py_XDECREF(tb);
			return -1;
		}
	}
	assert(current->run_info == tstate->dict);

green_updatecurrent_retry:
	/* update ts_current as soon as possible, in case of nested switches */
	Py_INCREF(current);
	previous = ts_current;
	ts_current = current;

	/* save ts_current as the current greenstack of its own thread */
	if (PyDict_SetItem(previous->run_info, ts_curkey, (PyObject*) previous)) {
		Py_DECREF(previous);
		Py_DECREF(current);
		Py_XDECREF(exc);
		Py_XDECREF(val);
		Py_XDECREF(tb);
		return -1;
	}
	Py_DECREF(previous);

	/* green_dealloc() cannot delete greenstacks from other threads, so
	   it stores them in the thread dict; delete them now. */
	deleteme = PyDict_GetItem(tstate->dict, ts_delkey);
	if (deleteme != NULL) {
		PyList_SetSlice(deleteme, 0, INT_MAX, NULL);
	}

	if (ts_current != current) {
		/* some Python code executed above and there was a thread switch,
		 * so ts_current points to some other thread again. We need to
		 * delete ts_curkey (it's likely there) and retry. */
		PyDict_DelItem(tstate->dict, ts_curkey);
		goto green_updatecurrent_retry;
	}

	/* release an extra reference */
	Py_DECREF(current);

	/* restore current exception */
	PyErr_Restore(exc, val, tb);

	/* thread switch could happen during PyErr_Restore, in that
	   case there's nothing to do except restart from scratch. */
	if (ts_current->run_info != tstate->dict)
		goto green_updatecurrent_restart;

	return 0;
}

static PyObject* green_statedict(PyGreenstack* g)
{
	while (!PyGreenstack_STARTED(g)) {
		g = g->parent;
		if (g == NULL) {
			/* garbage collected greenstack in chain */
			return NULL;
		}
	}
	return g->run_info;
}

/***********************************************************/

static void g_realswitchstack()
{
	PyThreadState *tstate;
	PyGreenstack *current;
	int recursion_depth;
	PyObject *exc_type, *exc_value, *exc_traceback;

	/* save state */
	tstate = PyThreadState_GET();
	current = ts_current;
	recursion_depth = tstate->recursion_depth;
	current->top_frame = tstate->frame;
	exc_type = tstate->exc_type;
	exc_value = tstate->exc_value;
	exc_traceback = tstate->exc_traceback;

	ts_origin = current;
	Py_INCREF(ts_target);
	ts_current = ts_target;

	coro_transfer(&current->context, &ts_target->context);

	/* restore state */
	tstate = PyThreadState_GET();
	tstate->recursion_depth = recursion_depth;
	tstate->frame = current->top_frame;
	tstate->exc_type = exc_type;
	tstate->exc_value = exc_value;
	tstate->exc_traceback = exc_traceback;
}

static void g_switchstack(PyGreenstack *target) {
	ts_target = target;
	PyGreenstack_CALL_SWITCH(statehandlers);
	ts_target = NULL;
}

static int g_create(PyGreenstack *self, PyObject *args, PyObject *kwargs);

#if GREENSTACK_USE_TRACING
static int
g_calltrace(PyObject* tracefunc, PyObject* event, PyGreenstack* origin, PyGreenstack* target)
{
	PyObject *retval;
	PyObject *exc_type, *exc_val, *exc_tb;
	PyThreadState *tstate;
	PyErr_Fetch(&exc_type, &exc_val, &exc_tb);
	tstate = PyThreadState_GET();
	tstate->tracing++;
	tstate->use_tracing = 0;
	retval = PyObject_CallFunction(tracefunc, "O(OO)", event, origin, target);
	tstate->tracing--;
	tstate->use_tracing = (tstate->tracing <= 0 &&
	                       ((tstate->c_tracefunc != NULL) ||
	                        (tstate->c_profilefunc != NULL)));
	if (retval == NULL) {
		/* In case of exceptions trace function is removed */
		if (PyDict_GetItem(tstate->dict, ts_tracekey))
			PyDict_DelItem(tstate->dict, ts_tracekey);
		Py_XDECREF(exc_type);
		Py_XDECREF(exc_val);
		Py_XDECREF(exc_tb);
		return -1;
	} else
		Py_DECREF(retval);
	PyErr_Restore(exc_type, exc_val, exc_tb);
	return 0;
}
#endif

static PyObject *
g_switch(PyGreenstack* target, PyObject* args, PyObject* kwargs)
{
	/* _consumes_ a reference to the args tuple and kwargs dict,
	   and return a new tuple reference */
	int err = 0;
	PyObject* run_info;

	/* check ts_current */
	if (!STATE_OK) {
		Py_XDECREF(args);
		Py_XDECREF(kwargs);
		return NULL;
	}
	run_info = green_statedict(target);
	if (run_info == NULL || run_info != ts_current->run_info) {
		Py_XDECREF(args);
		Py_XDECREF(kwargs);
		PyErr_SetString(PyExc_GreenstackError, run_info
		                ? "cannot switch to a different thread"
		                : "cannot switch to a garbage collected greenstack");
		return NULL;
	}

	ts_passaround_args = args;
	ts_passaround_kwargs = kwargs;

	/* find the real target by ignoring dead greenstacks, and if necessary
	 * starting a greenstack. */
	while (target) {
		if (PyGreenstack_ACTIVE(target)) {
			g_switchstack(target);
			break;
		}
		if (!PyGreenstack_STARTED(target)) {
			err = g_create(target, args, kwargs);
			if (err == 1) {
				continue;
			}
			break;
		}
		target = target->parent;
	}

	/* For a very short time, immediately after the 'atomic'
	   g_switchstack() call, global variables are in a known state.
	   We need to save everything we need, before it is destroyed
	   by calls into arbitrary Python code. */
	args = ts_passaround_args;
	ts_passaround_args = NULL;
	kwargs = ts_passaround_kwargs;
	ts_passaround_kwargs = NULL;
	if (err < 0) {
		/* Turn switch errors into switch throws */
		assert(ts_origin == NULL);
		Py_CLEAR(kwargs);
		Py_CLEAR(args);
	} else {
		PyGreenstack *origin;
#if GREENSTACK_USE_TRACING
		PyGreenstack *current;
		PyObject *tracefunc;
#endif
		origin = ts_origin;
		ts_origin = NULL;
#if GREENSTACK_USE_TRACING
		current = ts_current;
		if ((tracefunc = PyDict_GetItem(current->run_info, ts_tracekey)) != NULL) {
			Py_INCREF(tracefunc);
			if (g_calltrace(tracefunc, args ? ts_event_switch : ts_event_throw, origin, current) < 0) {
				/* Turn trace errors into switch throws */
				Py_CLEAR(kwargs);
				Py_CLEAR(args);
			}
			Py_DECREF(tracefunc);
		}
#endif
		Py_DECREF(origin);
	}

	/* We need to figure out what values to pass to the target greenstack
	   based on the arguments that have been passed to greenstack.switch(). If
	   switch() was just passed an arg tuple, then we'll just return that.
	   If only keyword arguments were passed, then we'll pass the keyword
	   argument dict. Otherwise, we'll create a tuple of (args, kwargs) and
	   return both. */
	if (kwargs == NULL)
	{
		return args;
	}
	else if (PyDict_Size(kwargs) == 0)
	{
		Py_DECREF(kwargs);
		return args;
	}
	else if (PySequence_Length(args) == 0)
	{
		Py_DECREF(args);
		return kwargs;
	}
	else
	{
		PyObject *tuple = PyTuple_New(2);
		if (tuple == NULL) {
			Py_DECREF(args);
			Py_DECREF(kwargs);
			return NULL;
		}
		PyTuple_SET_ITEM(tuple, 0, args);
		PyTuple_SET_ITEM(tuple, 1, kwargs);
		return tuple;
	}
}

static PyObject *
g_handle_exit(PyObject *result)
{
	if (result == NULL && PyErr_ExceptionMatches(PyExc_GreenstackExit))
	{
		/* catch and ignore GreenstackExit */
		PyObject *exc, *val, *tb;
		PyErr_Fetch(&exc, &val, &tb);
		if (val == NULL)
		{
			Py_INCREF(Py_None);
			val = Py_None;
		}
		result = val;
		Py_DECREF(exc);
		Py_XDECREF(tb);
	}
	if (result != NULL)
	{
		/* package the result into a 1-tuple */
		PyObject *r = result;
		result = PyTuple_New(1);
		if (result)
		{
			PyTuple_SET_ITEM(result, 0, r);
		}
		else
		{
			Py_DECREF(r);
		}
	}
	return result;
}

struct trampoline_data {
	PyGreenstack *self;
	PyObject *args;
	PyObject *run;
	PyObject *kwargs;
};

static void g_trampoline(struct trampoline_data *data) {
	PyThreadState *tstate;
	PyObject *result, *o;
	PyGreenstack *parent;

	PyGreenstack *self = data->self;
	PyObject *run = data->run;
	PyObject *args = data->args;
	PyObject *kwargs = data->kwargs;

	/* now use run_info to store the statedict */
	o = self->run_info;
	self->run_info = green_statedict(self->parent);
	Py_INCREF(self->run_info);
	Py_XDECREF(o);

#if GREENSTACK_USE_TRACING
	PyObject *tracefunc;
	if ((tracefunc = PyDict_GetItem(ts_current->run_info, ts_tracekey)) != NULL) {
		Py_INCREF(tracefunc);
		if (g_calltrace(tracefunc, args ? ts_event_switch : ts_event_throw, ts_origin, ts_current) < 0) {
			/* Turn trace errors into switch throws */
			Py_CLEAR(kwargs);
			Py_CLEAR(args);
		}
		Py_DECREF(tracefunc);
	}
#endif

	Py_DECREF(ts_origin);
	ts_origin = NULL;

	/* g_trampoline is responsible for setting up a nice clean slate */
	tstate = PyThreadState_GET();
	tstate->recursion_depth = 0;
	tstate->frame = NULL;
	tstate->exc_type = NULL;
	tstate->exc_value = NULL;
	tstate->exc_traceback = NULL;
	statehandler *handler = statehandlers;
	while (handler != NULL) {
		if (handler->stateinit != NULL) {
			handler->stateinit();
		}
		handler = handler->next;
	}

	if (args == NULL) {
		/* pending exception */
		result = NULL;
	} else {
		/* call g.run(*args, **kwargs) */
		result = PyEval_CallObjectWithKeywords(
			run, args, kwargs);
		Py_DECREF(args);
		Py_XDECREF(kwargs);
	}
	Py_DECREF(run);
	result = g_handle_exit(result);

	/* free the stack */
	if (STACK_CACHE_FULL) {
		coro_stack_free(&stack_cache[stack_cache_top--]);
	}
	stack_cache[stack_cache_top].sptr = self->stack;
	stack_cache[stack_cache_top].ssze = self->stack_size;
	stack_cache_top++;
	self->stack = NULL;
	/* leave stack_size where it is as an indication the greenstack was once alive */

	/* jump back to parent */
	for (parent = self->parent; parent != NULL; parent = parent->parent) {
		result = g_switch(parent, result, NULL);
		/* Return here means switch to parent failed,
		 * in which case we throw *current* exception
		 * to the next parent in chain.
		 */
		assert(result == NULL);
	}
	/* We ran out of parents, cannot continue */
	PyErr_WriteUnraisable((PyObject *) self);
	Py_FatalError("greenstack cannot continue");
}

static int g_create(PyGreenstack *self, PyObject *args, PyObject *kwargs)
{
	PyObject *run;
	PyObject *exc, *val, *tb;
	PyObject *run_info;

	struct coro_stack stack;
	struct trampoline_data data;

	/* save exception in case getattr clears it */
	PyErr_Fetch(&exc, &val, &tb);
	/* self.run is the object to call in the new greenstack */
	run = PyObject_GetAttrString((PyObject*) self, "run");
	if (run == NULL) {
		Py_XDECREF(exc);
		Py_XDECREF(val);
		Py_XDECREF(tb);
		return -1;
	}
	/* restore saved exception */
	PyErr_Restore(exc, val, tb);

	/* recheck the state in case getattr caused thread switches */
	if (!STATE_OK) {
		Py_DECREF(run);
		return -1;
	}

	/* recheck run_info in case greenstack reparented anywhere above */
	run_info = green_statedict(self);
	if (run_info == NULL || run_info != ts_current->run_info) {
		Py_DECREF(run);
		PyErr_SetString(PyExc_GreenstackError, run_info
		                ? "cannot switch to a different thread"
		                : "cannot switch to a garbage collected greenstack");
		return -1;
	}

	/* by the time we got here another start could happen elsewhere,
	 * that means it should now be a regular switch
	 */
	if (PyGreenstack_STARTED(self)) {
		Py_DECREF(run);
		ts_passaround_args = args;
		ts_passaround_kwargs = kwargs;
		return 1;
	}

	/* start the greenstack */
	/* default stack size is 256k * sizeof(void *) */
	if (stack_cache_top != 0) {
		stack = stack_cache[--stack_cache_top];
	} else {
		if (!coro_stack_alloc(&stack, 0)) {
			Py_DECREF(run);
			return -1;
		}
	}
	self->stack = stack.sptr;
	self->stack_size = stack.ssze;
	data.self = self;
	data.run = run;
	data.args = args;
	data.kwargs = kwargs;
	coro_create(&self->context, (coro_func) g_trampoline, &data, self->stack, self->stack_size);
	self->top_frame = NULL;

	g_switchstack(self);

	return 0;
}


/***********************************************************/


static PyObject* green_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject* o = PyBaseObject_Type.tp_new(type, ts_empty_tuple, ts_empty_dict);
	if (o != NULL) {
		if (!STATE_OK) {
			Py_DECREF(o);
			return NULL;
		}
		Py_INCREF(ts_current);
		((PyGreenstack*) o)->parent = ts_current;
	}
	return o;
}

static int green_setrun(PyGreenstack* self, PyObject* nrun, void* c);
static int green_setparent(PyGreenstack* self, PyObject* nparent, void* c);

static int green_init(PyGreenstack *self, PyObject *args, PyObject *kwargs)
{
	PyObject *run = NULL;
	PyObject* nparent = NULL;
	static char *kwlist[] = {"run", "parent", 0};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OO:green", kwlist,
	                                 &run, &nparent))
		return -1;

	if (run != NULL) {
		if (green_setrun(self, run, NULL))
			return -1;
	}
	if (nparent != NULL && nparent != Py_None)
		return green_setparent(self, nparent, NULL);
	return 0;
}

static int kill_greenstack(PyGreenstack* self)
{
	/* Cannot raise an exception to kill the greenstack if
	   it is not running in the same thread! */
	if (self->run_info == PyThreadState_GET()->dict) {
		/* The dying greenstack cannot be a parent of ts_current
		   because the 'parent' field chain would hold a
		   reference */
		PyObject* result;
		PyGreenstack* oldparent;
		PyGreenstack* tmp;
		if (!STATE_OK) {
			return -1;
		}
		oldparent = self->parent;
		self->parent = ts_current;
		Py_INCREF(self->parent);
		/* Send the greenstack a GreenstackExit exception. */
		PyErr_SetNone(PyExc_GreenstackExit);
		result = g_switch(self, NULL, NULL);
		tmp = self->parent;
		self->parent = oldparent;
		Py_XDECREF(tmp);
		if (result == NULL)
			return -1;
		Py_DECREF(result);
		return 0;
	}
	else {
		/* Not the same thread! Temporarily save the greenstack
		   into its thread's ts_delkey list. */
		PyObject* lst;
		lst = PyDict_GetItem(self->run_info, ts_delkey);
		if (lst == NULL) {
			lst = PyList_New(0);
			if (lst == NULL || PyDict_SetItem(self->run_info,
			                                  ts_delkey, lst) < 0)
				return -1;
		}
		if (PyList_Append(lst, (PyObject*) self) < 0)
			return -1;
		if (!STATE_OK)  /* to force ts_delkey to be reconsidered */
			return -1;
		return 0;
	}
}

#if GREENSTACK_USE_GC
static int
green_traverse(PyGreenstack *self, visitproc visit, void *arg)
{
	/* We must only visit referenced objects, i.e. only objects
	   Py_INCREF'ed by this greenstack (directly or indirectly):
	   - stack_prev is not visited: holds previous stack pointer, but it's not referenced
	   - frames are not visited: alive greenstacks are not garbage collected anyway */
	Py_VISIT((PyObject*)self->parent);
	Py_VISIT(self->run_info);
	Py_VISIT(self->dict);
	return 0;
}

static int green_is_gc(PyGreenstack* self)
{
	/* Main greenstack can be garbage collected since it can only
	   become unreachable if the underlying thread exited.
	   Active greenstack cannot be garbage collected, however. */
	if (PyGreenstack_MAIN(self) || !PyGreenstack_ACTIVE(self))
		return 1;
	return 0;
}

static int green_clear(PyGreenstack* self)
{
	/* Greenstack is only cleared if it is about to be collected.
	   Since active greenstacks are not garbage collectable, we can
	   be sure that, even if they are deallocated during clear,
	   nothing they reference is in unreachable or finalizers,
	   so even if it switches we are relatively safe. */
	Py_CLEAR(self->parent);
	Py_CLEAR(self->run_info);
	Py_CLEAR(self->dict);
	return 0;
}
#endif

static void green_dealloc_safe(PyGreenstack* self)
{
	PyObject *error_type, *error_value, *error_traceback;

	if (PyGreenstack_ACTIVE(self) && self->run_info != NULL && !PyGreenstack_MAIN(self)) {
		/* Hacks hacks hacks copied from instance_dealloc() */
		/* Temporarily resurrect the greenstack. */
		assert(Py_REFCNT(self) == 0);
		Py_REFCNT(self) = 1;
		/* Save the current exception, if any. */
		PyErr_Fetch(&error_type, &error_value, &error_traceback);
		if (kill_greenstack(self) < 0) {
			PyErr_WriteUnraisable((PyObject*) self);
			/* XXX what else should we do? */
		}
		/* Check for no resurrection must be done while we keep
		 * our internal reference, otherwise PyFile_WriteObject
		 * causes recursion if using Py_INCREF/Py_DECREF
		 */
		if (Py_REFCNT(self) == 1 && PyGreenstack_ACTIVE(self)) {
			/* Not resurrected, but still not dead!
			   XXX what else should we do? we complain. */
			PyObject* f = PySys_GetObject("stderr");
			Py_INCREF(self); /* leak! */
			if (f != NULL) {
				PyFile_WriteString("GreenstackExit did not kill ",
				                   f);
				PyFile_WriteObject((PyObject*) self, f, 0);
				PyFile_WriteString("\n", f);
			}
		}
		/* Restore the saved exception. */
		PyErr_Restore(error_type, error_value, error_traceback);
		/* Undo the temporary resurrection; can't use DECREF here,
		 * it would cause a recursive call.
		 */
		assert(Py_REFCNT(self) > 0);
		if (--Py_REFCNT(self) != 0) {
			/* Resurrected! */
			Py_ssize_t refcnt = Py_REFCNT(self);
			_Py_NewReference((PyObject*) self);
			Py_REFCNT(self) = refcnt;
#if GREENSTACK_USE_GC
			PyObject_GC_Track((PyObject *)self);
#endif
			_Py_DEC_REFTOTAL;
#ifdef COUNT_ALLOCS
			--Py_TYPE(self)->tp_frees;
			--Py_TYPE(self)->tp_allocs;
#endif /* COUNT_ALLOCS */
			return;
		}
	}
	if (self->weakreflist != NULL)
		PyObject_ClearWeakRefs((PyObject *) self);
	Py_CLEAR(self->parent);
	Py_CLEAR(self->run_info);
	Py_CLEAR(self->dict);
	Py_TYPE(self)->tp_free((PyObject*) self);
}

#if GREENSTACK_USE_GC
static void green_dealloc(PyGreenstack* self)
{
	PyObject_GC_UnTrack((PyObject *)self);
	if (PyObject_IS_GC((PyObject *)self)) {
		Py_TRASHCAN_SAFE_BEGIN(self);
		green_dealloc_safe(self);
		Py_TRASHCAN_SAFE_END(self);
	} else {
		/* This object cannot be garbage collected, so trashcan is not allowed */
		green_dealloc_safe(self);
	}
}
#else
#define green_dealloc green_dealloc_safe
#endif

static PyObject* single_result(PyObject* results)
{
	if (results != NULL && PyTuple_Check(results) &&
	    PyTuple_GET_SIZE(results) == 1) {
		PyObject *result = PyTuple_GET_ITEM(results, 0);
		Py_INCREF(result);
		Py_DECREF(results);
		return result;
	}
	else
		return results;
}

static PyObject *
throw_greenstack(PyGreenstack *self, PyObject *typ, PyObject *val, PyObject *tb)
{
	/* Note: _consumes_ a reference to typ, val, tb */
	PyObject *result = NULL;
	PyErr_Restore(typ, val, tb);
	if (PyGreenstack_STARTED(self) && !PyGreenstack_ACTIVE(self))
	{
		/* dead greenstack: turn GreenstackExit into a regular return */
		result = g_handle_exit(result);
	}
	return single_result(g_switch(self, result, NULL));
}

PyDoc_STRVAR(green_switch_doc,
"switch(*args, **kwargs)\n"
"\n"
"Switch execution to this greenstack.\n"
"\n"
"If this greenstack has never been run, then this greenstack\n"
"will be switched to using the body of self.run(*args, **kwargs).\n"
"\n"
"If the greenstack is active (has been run, but was switch()'ed\n"
"out before leaving its run function), then this greenstack will\n"
"be resumed and the return value to its switch call will be\n"
"None if no arguments are given, the given argument if one\n"
"argument is given, or the args tuple and keyword args dict if\n"
"multiple arguments are given.\n"
"\n"
"If the greenstack is dead, or is the current greenstack then this\n"
"function will simply return the arguments using the same rules as\n"
"above.\n");

static PyObject* green_switch(
	PyGreenstack* self,
	PyObject* args,
	PyObject* kwargs)
{
	Py_INCREF(args);
	Py_XINCREF(kwargs);
	return single_result(g_switch(self, args, kwargs));
}

/* Macros required to support Python < 2.6 for green_throw() */
#ifndef PyExceptionClass_Check
#  define PyExceptionClass_Check    PyClass_Check
#endif
#ifndef PyExceptionInstance_Check
#  define PyExceptionInstance_Check PyInstance_Check
#endif
#ifndef PyExceptionInstance_Class
#  define PyExceptionInstance_Class(x) \
	((PyObject *) ((PyInstanceObject *)(x))->in_class)
#endif

PyDoc_STRVAR(green_throw_doc,
"Switches execution to the greenstack ``g``, but immediately raises the\n"
"given exception in ``g``.  If no argument is provided, the exception\n"
"defaults to ``greenstack.GreenstackExit``.  The normal exception\n"
"propagation rules apply, as described above.  Note that calling this\n"
"method is almost equivalent to the following::\n"
"\n"
"    def raiser():\n"
"        raise typ, val, tb\n"
"    g_raiser = greenstack(raiser, parent=g)\n"
"    g_raiser.switch()\n"
"\n"
"except that this trick does not work for the\n"
"``greenstack.GreenstackExit`` exception, which would not propagate\n"
"from ``g_raiser`` to ``g``.\n");

static PyObject *
green_throw(PyGreenstack *self, PyObject *args)
{
	PyObject *typ = PyExc_GreenstackExit;
	PyObject *val = NULL;
	PyObject *tb = NULL;

	if (!PyArg_ParseTuple(args, "|OOO:throw", &typ, &val, &tb))
	{
		return NULL;
	}

	/* First, check the traceback argument, replacing None, with NULL */
	if (tb == Py_None)
	{
		tb = NULL;
	}
	else if (tb != NULL && !PyTraceBack_Check(tb))
	{
		PyErr_SetString(
			PyExc_TypeError,
			"throw() third argument must be a traceback object");
		return NULL;
	}

	Py_INCREF(typ);
	Py_XINCREF(val);
	Py_XINCREF(tb);

	if (PyExceptionClass_Check(typ))
	{
		PyErr_NormalizeException(&typ, &val, &tb);
	}
	else if (PyExceptionInstance_Check(typ))
	{
		/* Raising an instance. The value should be a dummy. */
		if (val && val != Py_None)
		{
			PyErr_SetString(
				PyExc_TypeError,
				"instance exception may not have a separate value");
			goto failed_throw;
		}
		else
		{
			/* Normalize to raise <class>, <instance> */
			Py_XDECREF(val);
			val = typ;
			typ = PyExceptionInstance_Class(typ);
			Py_INCREF(typ);
		}
	}
	else
	{
		/* Not something you can raise. throw() fails. */
		PyErr_Format(
			PyExc_TypeError,
			"exceptions must be classes, or instances, not %s",
			Py_TYPE(typ)->tp_name);
		goto failed_throw;
	}

	return throw_greenstack(self, typ, val, tb);

failed_throw:
	/* Didn't use our arguments, so restore their original refcounts */
	Py_DECREF(typ);
	Py_XDECREF(val);
	Py_XDECREF(tb);
	return NULL;
}

static int green_bool(PyGreenstack* self)
{
	return PyGreenstack_ACTIVE(self);
}

static PyObject* green_getdict(PyGreenstack* self, void* c)
{
	if (self->dict == NULL) {
		self->dict = PyDict_New();
		if (self->dict == NULL)
			return NULL;
	}
	Py_INCREF(self->dict);
	return self->dict;
}

static int green_setdict(PyGreenstack* self, PyObject* val, void* c)
{
	PyObject* tmp;

	if (val == NULL) {
		PyErr_SetString(PyExc_TypeError, "__dict__ may not be deleted");
		return -1;
	}
	if (!PyDict_Check(val)) {
		PyErr_SetString(PyExc_TypeError, "__dict__ must be a dictionary");
		return -1;
	}
	tmp = self->dict;
	Py_INCREF(val);
	self->dict = val;
	Py_XDECREF(tmp);
	return 0;
}

static PyObject* green_getdead(PyGreenstack* self, void* c)
{
	if (PyGreenstack_ACTIVE(self) || !PyGreenstack_STARTED(self))
		Py_RETURN_FALSE;
	else
		Py_RETURN_TRUE;
}

static PyObject* green_getrun(PyGreenstack* self, void* c)
{
	if (PyGreenstack_STARTED(self) || self->run_info == NULL) {
		PyErr_SetString(PyExc_AttributeError, "run");
		return NULL;
	}
	Py_INCREF(self->run_info);
	return self->run_info;
}

static int green_setrun(PyGreenstack* self, PyObject* nrun, void* c)
{
	PyObject* o;
	if (PyGreenstack_STARTED(self)) {
		PyErr_SetString(PyExc_AttributeError,
		                "run cannot be set "
		                "after the start of the greenstack");
		return -1;
	}
	o = self->run_info;
	self->run_info = nrun;
	Py_XINCREF(nrun);
	Py_XDECREF(o);
	return 0;
}

static PyObject* green_getparent(PyGreenstack* self, void* c)
{
	PyObject* result = self->parent ? (PyObject*) self->parent : Py_None;
	Py_INCREF(result);
	return result;
}

static int green_setparent(PyGreenstack* self, PyObject* nparent, void* c)
{
	PyGreenstack* p;
	PyObject* run_info = NULL;
	if (nparent == NULL) {
		PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
		return -1;
	}
	if (!PyGreenstack_Check(nparent)) {
		PyErr_SetString(PyExc_TypeError, "parent must be a greenstack");
		return -1;
	}
	for (p=(PyGreenstack*) nparent; p; p=p->parent) {
		if (p == self) {
			PyErr_SetString(PyExc_ValueError, "cyclic parent chain");
			return -1;
		}
		run_info = PyGreenstack_ACTIVE(p) ? p->run_info : NULL;
	}
	if (run_info == NULL) {
		PyErr_SetString(PyExc_ValueError, "parent must not be garbage collected");
		return -1;
	}
	if (PyGreenstack_STARTED(self) && self->run_info != run_info) {
		PyErr_SetString(PyExc_ValueError, "parent cannot be on a different thread");
		return -1;
	}
	p = self->parent;
	self->parent = (PyGreenstack*) nparent;
	Py_INCREF(nparent);
	Py_XDECREF(p);
	return 0;
}

static PyObject* green_getframe(PyGreenstack* self, void* c)
{
	PyObject* result = self->top_frame ? (PyObject*) self->top_frame : Py_None;
	Py_INCREF(result);
	return result;
}

static PyObject* green_getstate(PyGreenstack* self)
{
	PyErr_Format(PyExc_TypeError,
	             "cannot serialize '%s' object",
	             Py_TYPE(self)->tp_name);
	return NULL;
}


/*****************************************************************************
 * C interface
 *
 * These are exported using the CObject API
 */

static PyGreenstack *
PyGreenstack_GetCurrent(void)
{
	if (!STATE_OK) {
		return NULL;
	}
	Py_INCREF(ts_current);
	return ts_current;
}

static int
PyGreenstack_SetParent(PyGreenstack *g, PyGreenstack *nparent)
{
	if (!PyGreenstack_Check(g)) {
		PyErr_SetString(PyExc_TypeError, "parent must be a greenstack");
		return -1;
	}

	return green_setparent((PyGreenstack*) g, (PyObject *) nparent, NULL);
}

static PyGreenstack *
PyGreenstack_New(PyObject *run, PyGreenstack *parent)
{
	PyGreenstack* g = NULL;

	g = (PyGreenstack *) PyType_GenericAlloc(&PyGreenstack_Type, 0);
	if (g == NULL) {
		return NULL;
	}

	if (run != NULL) {
		Py_INCREF(run);
		g->run_info = run;
	}

	if (parent != NULL) {
		if (PyGreenstack_SetParent(g, parent)) {
			Py_DECREF(g);
			return NULL;
		}
	} else {
		if ((g->parent = PyGreenstack_GetCurrent()) == NULL) {
			Py_DECREF(g);
			return NULL;
		}
	}

	return g;
}

static PyObject *
PyGreenstack_Switch(PyGreenstack *g, PyObject *args, PyObject *kwargs)
{
	PyGreenstack *self = (PyGreenstack *) g;

	if (!PyGreenstack_Check(self)) {
		PyErr_BadArgument();
		return NULL;
	}

	if (args == NULL) {
		args = Py_BuildValue("()");
	}
	else {
		Py_INCREF(args);
	}

	if (kwargs != NULL && PyDict_Check(kwargs)) {
		Py_INCREF(kwargs);
	}
	else {
		kwargs = NULL;
	}

	return single_result(g_switch(self, args, kwargs));
}

static PyObject *
PyGreenstack_Throw(PyGreenstack *self, PyObject *typ, PyObject *val, PyObject *tb)
{
	if (!PyGreenstack_Check(self)) {
		PyErr_BadArgument();
		return NULL;
	}
	Py_INCREF(typ);
	Py_XINCREF(val);
	Py_XINCREF(tb);
	return throw_greenstack(self, typ, val, tb);
}

int PyGreenstack_AddStateHandler(switchwrapperfunc wrapper, stateinitfunc stateinit) {
	statehandler *new_handler = (statehandler *) malloc(sizeof(statehandler));
	if (new_handler == NULL) {
		PyErr_NoMemory();
		return -1;
	}
	new_handler->wrapper = wrapper;
	new_handler->stateinit = stateinit;
	new_handler->next = statehandlers;
	statehandlers = new_handler;
	return 0;
}

/** End C API ****************************************************************/

static PyMethodDef green_methods[] = {
	{"switch", (PyCFunction)green_switch,
	 METH_VARARGS | METH_KEYWORDS, green_switch_doc},
	{"throw",  (PyCFunction)green_throw,  METH_VARARGS, green_throw_doc},
	{"__getstate__", (PyCFunction)green_getstate, METH_NOARGS, NULL},
	{NULL,     NULL} /* sentinel */
};

static PyGetSetDef green_getsets[] = {
	{"__dict__", (getter)green_getdict,
	             (setter)green_setdict, /*XXX*/ NULL},
	{"run",      (getter)green_getrun,
	             (setter)green_setrun, /*XXX*/ NULL},
	{"parent",   (getter)green_getparent,
	             (setter)green_setparent, /*XXX*/ NULL},
	{"gr_frame", (getter)green_getframe,
	             NULL, /*XXX*/ NULL},
	{"dead",     (getter)green_getdead,
	             NULL, /*XXX*/ NULL},
	{NULL}
};

static PyNumberMethods green_as_number = {
	NULL,                /* nb_add */
	NULL,                /* nb_subtract */
	NULL,                /* nb_multiply */
#if PY_MAJOR_VERSION < 3
	NULL,                /* nb_divide */
#endif
	NULL,                /* nb_remainder */
	NULL,                /* nb_divmod */
	NULL,                /* nb_power */
	NULL,                /* nb_negative */
	NULL,                /* nb_positive */
	NULL,                /* nb_absolute */
	(inquiry)green_bool, /* nb_bool */
};


PyTypeObject PyGreenstack_Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"greenstack.greenstack",                    /* tp_name */
	sizeof(PyGreenstack),                     /* tp_basicsize */
	0,                                      /* tp_itemsize */
	/* methods */
	(destructor)green_dealloc,              /* tp_dealloc */
	0,                                      /* tp_print */
	0,                                      /* tp_getattr */
	0,                                      /* tp_setattr */
	0,                                      /* tp_compare */
	0,                                      /* tp_repr */
	&green_as_number,                       /* tp_as _number*/
	0,                                      /* tp_as _sequence*/
	0,                                      /* tp_as _mapping*/
	0,                                      /* tp_hash */
	0,                                      /* tp_call */
	0,                                      /* tp_str */
	0,                                      /* tp_getattro */
	0,                                      /* tp_setattro */
	0,                                      /* tp_as_buffer*/
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | GREENSTACK_GC_FLAGS, /* tp_flags */
	"greenstack(run=None, parent=None) -> greenstack\n\n"
	"Creates a new greenstack object (without running it).\n\n"
	" - *run* -- The callable to invoke.\n"
	" - *parent* -- The parent greenstack. The default is the current "
	"greenstack.",                            /* tp_doc */
	(traverseproc)GREENSTACK_tp_traverse,     /* tp_traverse */
	(inquiry)GREENSTACK_tp_clear,             /* tp_clear */
	0,                                      /* tp_richcompare */
	offsetof(PyGreenstack, weakreflist),      /* tp_weaklistoffset */
	0,                                      /* tp_iter */
	0,                                      /* tp_iternext */
	green_methods,                          /* tp_methods */
	0,                                      /* tp_members */
	green_getsets,                          /* tp_getset */
	0,                                      /* tp_base */
	0,                                      /* tp_dict */
	0,                                      /* tp_descr_get */
	0,                                      /* tp_descr_set */
	offsetof(PyGreenstack, dict),             /* tp_dictoffset */
	(initproc)green_init,                   /* tp_init */
	GREENSTACK_tp_alloc,                      /* tp_alloc */
	green_new,                              /* tp_new */
	GREENSTACK_tp_free,                       /* tp_free */
	(inquiry)GREENSTACK_tp_is_gc,             /* tp_is_gc */
};

static PyObject* mod_getcurrent(PyObject* self)
{
	if (!STATE_OK)
		return NULL;
	Py_INCREF(ts_current);
	return (PyObject*) ts_current;
}

#if GREENSTACK_USE_TRACING
static PyObject* mod_settrace(PyObject* self, PyObject* args)
{
	int err;
	PyObject* previous;
	PyObject* tracefunc;
	PyGreenstack* current;
	if (!PyArg_ParseTuple(args, "O", &tracefunc))
		return NULL;
	if (!STATE_OK)
		return NULL;
	current = ts_current;
	previous = PyDict_GetItem(current->run_info, ts_tracekey);
	if (previous == NULL)
		previous = Py_None;
	Py_INCREF(previous);
	if (tracefunc == Py_None)
		err = previous != Py_None ? PyDict_DelItem(current->run_info, ts_tracekey) : 0;
	else
		err = PyDict_SetItem(current->run_info, ts_tracekey, tracefunc);
	if (err < 0)
		Py_CLEAR(previous);
	return previous;
}

static PyObject* mod_gettrace(PyObject* self)
{
	PyObject* tracefunc;
	if (!STATE_OK)
		return NULL;
	tracefunc = PyDict_GetItem(ts_current->run_info, ts_tracekey);
	if (tracefunc == NULL)
		tracefunc = Py_None;
	Py_INCREF(tracefunc);
	return tracefunc;
}
#endif

static PyMethodDef GreenMethods[] = {
	{"getcurrent", (PyCFunction)mod_getcurrent, METH_NOARGS, /*XXX*/ NULL},
#if GREENSTACK_USE_TRACING
	{"settrace", (PyCFunction)mod_settrace, METH_VARARGS, NULL},
	{"gettrace", (PyCFunction)mod_gettrace, METH_NOARGS, NULL},
#endif
	{NULL,     NULL}        /* Sentinel */
};

static char* copy_on_greentype[] = {
	"getcurrent",
	"error",
	"GreenstackExit",
#if GREENSTACK_USE_TRACING
	"settrace",
	"gettrace",
#endif
	NULL
};

#if PY_MAJOR_VERSION >= 3
#define INITERROR return NULL

static struct PyModuleDef greenstack_module_def = {
	PyModuleDef_HEAD_INIT,
	"greenstack",
	NULL,
	-1,
	GreenMethods,
};

PyMODINIT_FUNC
PyInit_greenstack(void)
#else
#define INITERROR return

PyMODINIT_FUNC
initgreenstack(void)
#endif
{
	PyObject* m = NULL;
	char** p = NULL;
	PyObject *c_api_object;
	static void *_PyGreenstack_API[PyGreenstack_API_pointers];

#if PY_MAJOR_VERSION >= 3
	m = PyModule_Create(&greenstack_module_def);
#else
	m = Py_InitModule("greenstack", GreenMethods);
#endif
	if (m == NULL)
	{
		INITERROR;
	}

	if (PyModule_AddStringConstant(m, "__version__", GREENSTACK_VERSION) < 0)
	{
		INITERROR;
	}

#if PY_MAJOR_VERSION >= 3
	ts_curkey = PyUnicode_InternFromString("__greenstack_ts_curkey");
	ts_delkey = PyUnicode_InternFromString("__greenstack_ts_delkey");
#if GREENSTACK_USE_TRACING
	ts_tracekey = PyUnicode_InternFromString("__greenstack_ts_tracekey");
	ts_event_switch = PyUnicode_InternFromString("switch");
	ts_event_throw = PyUnicode_InternFromString("throw");
#endif
#else
	ts_curkey = PyString_InternFromString("__greenstack_ts_curkey");
	ts_delkey = PyString_InternFromString("__greenstack_ts_delkey");
#if GREENSTACK_USE_TRACING
	ts_tracekey = PyString_InternFromString("__greenstack_ts_tracekey");
	ts_event_switch = PyString_InternFromString("switch");
	ts_event_throw = PyString_InternFromString("throw");
#endif
#endif
	if (ts_curkey == NULL || ts_delkey == NULL)
	{
		INITERROR;
	}
	if (PyType_Ready(&PyGreenstack_Type) < 0)
	{
		INITERROR;
	}
	PyExc_GreenstackError = PyErr_NewException("greenstack.error", NULL, NULL);
	if (PyExc_GreenstackError == NULL)
	{
		INITERROR;
	}
#if PY_MAJOR_VERSION >= 3 || (PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 5)
	PyExc_GreenstackExit = PyErr_NewException("greenstack.GreenstackExit",
	                                        PyExc_BaseException, NULL);
#else
	PyExc_GreenstackExit = PyErr_NewException("greenstack.GreenstackExit",
	                                        NULL, NULL);
#endif
	if (PyExc_GreenstackExit == NULL)
	{
		INITERROR;
	}

	ts_empty_tuple = PyTuple_New(0);
	if (ts_empty_tuple == NULL)
	{
		INITERROR;
	}

	ts_empty_dict = PyDict_New();
	if (ts_empty_dict == NULL)
	{
		INITERROR;
	}

	ts_current = green_create_main();
	if (ts_current == NULL)
	{
		INITERROR;
	}

	Py_INCREF(&PyGreenstack_Type);
	PyModule_AddObject(m, "greenstack", (PyObject*) &PyGreenstack_Type);
	Py_INCREF(PyExc_GreenstackError);
	PyModule_AddObject(m, "error", PyExc_GreenstackError);
	Py_INCREF(PyExc_GreenstackExit);
	PyModule_AddObject(m, "GreenstackExit", PyExc_GreenstackExit);
	PyModule_AddObject(m, "GREENSTACK_USE_GC", PyBool_FromLong(GREENSTACK_USE_GC));
	PyModule_AddObject(m, "GREENSTACK_USE_TRACING", PyBool_FromLong(GREENSTACK_USE_TRACING));

	/* also publish module-level data as attributes of the greentype. */
	for (p=copy_on_greentype; *p; p++) {
		PyObject* o = PyObject_GetAttrString(m, *p);
		if (!o) continue;
		PyDict_SetItemString(PyGreenstack_Type.tp_dict, *p, o);
		Py_DECREF(o);
	}

	/*
	 * Expose C API
	 */

	/* types */
	_PyGreenstack_API[PyGreenstack_Type_NUM] = (void *) &PyGreenstack_Type;

	/* exceptions */
	_PyGreenstack_API[PyExc_GreenstackError_NUM] = (void *) PyExc_GreenstackError;
	_PyGreenstack_API[PyExc_GreenstackExit_NUM] = (void *) PyExc_GreenstackExit;

	/* methods */
	_PyGreenstack_API[PyGreenstack_New_NUM] = (void *) PyGreenstack_New;
	_PyGreenstack_API[PyGreenstack_GetCurrent_NUM] =
		(void *) PyGreenstack_GetCurrent;
	_PyGreenstack_API[PyGreenstack_Throw_NUM] = (void *) PyGreenstack_Throw;
	_PyGreenstack_API[PyGreenstack_Switch_NUM] = (void *) PyGreenstack_Switch;
	_PyGreenstack_API[PyGreenstack_SetParent_NUM] =
		(void *) PyGreenstack_SetParent;
	_PyGreenstack_API[PyGreenstack_AddStateHandler_NUM] =
		(void *) PyGreenstack_AddStateHandler;

#ifdef GREENSTACK_USE_PYCAPSULE
	c_api_object = PyCapsule_New((void *) _PyGreenstack_API, "greenstack._C_API", NULL);
#else
	c_api_object = PyCObject_FromVoidPtr((void *) _PyGreenstack_API, NULL);
#endif
	if (c_api_object != NULL)
	{
		PyModule_AddObject(m, "_C_API", c_api_object);
	}

#if PY_MAJOR_VERSION >= 3
	return m;
#endif
}
