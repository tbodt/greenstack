===============================================
Greenstack: Cooperative green threads in Python
===============================================
.. toctree::
   :maxdepth: 2

Introduction
============

Greenstack is a fork of greenlet, which is a spin-off of `Stackless`_, a
version of CPython that supports micro-threads called "tasklets".  Tasklets run
pseudo-concurrently (typically in a single or a few OS-level threads) and are
synchronized with data exchanges on "channels".

A "greenlet", on the other hand, is a still more primitive notion of
micro-thread with no implicit scheduling; coroutines, in other words.  This is
useful when you want to control exactly when your code runs.  You can build
custom scheduled micro-threads on top of greenlet; however, it seems that
greenlets are useful on their own as a way to make advanced control flow
structures.  For example, we can recreate generators; the difference with
Python's own generators is that our generators can call nested functions and
the nested functions can yield values too.  (Additionally, you don't need a
"yield" keyword.  See the example in ``test/test_generator.py``). 

Greenstack is a C extension module for the regular unmodified interpreter.

.. _`Stackless`: http://www.stackless.com

Differences from Greenlet
-------------------------

* In Greenlet, each greenlet shares the same stack, and
  the stack for each greenlet is copied into the heap when the stack space is
  needed for another greenlet. Greenstack allocates a separate stack for each
  greenlet, and reuses stacks from greenlets that have exited for new
  greenlets.

* Greenlet implements stack switching using assembly language code. Greenstack
  uses libcoro to do stack switching, which can use either assembler code,
  setjmp/sigaltstack, ucontext, or fibers depending on the platform.

* Greenstack renames the greenlet type to greenstack and GreenletExit to
  GreenstackExit. However, the old names are still available for
  compatibility's sake.

* Greenstack includes an API for custom state saving/restoring available from
  C.

Example
-------

Let's consider a system controlled by a terminal-like console, where the user
types commands.  Assume that the input comes character by character.  In such
a system, there will typically be a loop like the following one::

    def process_commands(*args):
        while True:
            line = ''
            while not line.endswith('\n'):
                line += read_next_char()
            if line == 'quit\n':
                print "are you sure?"
                if read_next_char() != 'y':
                    continue    # ignore the command
            process_command(line)

Now assume that you want to plug this program into a GUI.  Most GUI toolkits
are event-based.  They will invoke a call-back for each character the user
presses.  [Replace "GUI" with "XML expat parser" if that rings more bells to
you ``:-)``]  In this setting, it is difficult to implement the
read_next_char() function needed by the code above.  We have two incompatible
functions::

    def event_keydown(key):
        ?? 

    def read_next_char():
        ?? should wait for the next event_keydown() call

You might consider doing that with threads.  Greenstacks are an alternate
solution that don't have the related locking and shutdown problems.  You
start the process_commands() function in its own, separate greenstack, and
then you exchange the keypresses with it as follows::

    def event_keydown(key):
             # jump into g_processor, sending it the key
        g_processor.switch(key)

    def read_next_char():
            # g_self is g_processor in this simple example
        g_self = greenstack.getcurrent()
            # jump to the parent (main) greenstack, waiting for the next key
        next_char = g_self.parent.switch()
        return next_char

    g_processor = greenstack(process_commands)
    g_processor.switch(*args)   # input arguments to process_commands()

    gui.mainloop()

In this example, the execution flow is: when read_next_char() is called, it
is part of the g_processor greenstack, so when it switches to its parent
greenstack, it resumes execution in the top-level main loop (the GUI).  When
the GUI calls event_keydown(), it switches to g_processor, which means that
the execution jumps back wherever it was suspended in that greenstack -- in
this case, to the switch() instruction in read_next_char() -- and the ``key``
argument in event_keydown() is passed as the return value of the switch() in
read_next_char().

Note that read_next_char() will be suspended and resumed with its call stack
preserved, so that it will itself return to different positions in
process_commands() depending on where it was originally called from.  This
allows the logic of the program to be kept in a nice control-flow way; we
don't have to completely rewrite process_commands() to turn it into a state
machine.


Usage
=====

Introduction
------------

A "greenstack" is a small independent pseudo-thread.  Think about it as a
small stack of frames; the outermost (bottom) frame is the initial
function you called, and the innermost frame is the one in which the
greenstack is currently paused.  You work with greenstacks by creating a
number of such stacks and jumping execution between them.  Jumps are never
implicit: a greenstack must choose to jump to another greenstack, which will
cause the former to suspend and the latter to resume where it was
suspended.  Jumping between greenstacks is called "switching".

When you create a greenstack, it gets an initially empty stack; when you
first switch to it, it starts to run a specified function, which may call
other functions, switch out of the greenstack, etc.  When eventually the
outermost function finishes its execution, the greenstack's stack becomes
empty again and the greenstack is "dead".  Greenstacks can also die of an
uncaught exception.

For example::

    from greenstack import greenstack

    def test1():
        print 12
        gr2.switch()
        print 34

    def test2():
        print 56
        gr1.switch()
        print 78

    gr1 = greenstack(test1)
    gr2 = greenstack(test2)
    gr1.switch()

The last line jumps to test1, which prints 12, jumps to test2, prints 56,
jumps back into test1, prints 34; and then test1 finishes and gr1 dies.  
At this point, the execution comes back to the original ``gr1.switch()``
call.  Note that 78 is never printed.

Parents
-------

Let's see where execution goes when a greenstack dies.  Every greenstack has a
"parent" greenstack.  The parent greenstack is initially the one in which the
greenstack was created (this can be changed at any time).  The parent is
where execution continues when a greenstack dies.  This way, greenstacks are
organized in a tree.  Top-level code that doesn't run in a user-created
greenstack runs in the implicit "main" greenstack, which is the root of the
tree.

In the above example, both gr1 and gr2 have the main greenstack as a parent.  
Whenever one of them dies, the execution comes back to "main".

Uncaught exceptions are propagated into the parent, too.  For example, if
the above test2() contained a typo, it would generate a NameError that
would kill gr2, and the exception would go back directly into "main".  
The traceback would show test2, but not test1.  Remember, switches are not
calls, but transfer of execution between parallel "stack containers", and
the "parent" defines which stack logically comes "below" the current one.

Instantiation
-------------

``greenstack.greenstack`` is the greenstack type, which supports the following
operations:

``greenstack(run=None, parent=None)``
    Create a new greenstack object (without running it).  ``run`` is the
    callable to invoke, and ``parent`` is the parent greenstack, which
    defaults to the current greenstack.

``greenstack.getcurrent()``
    Returns the current greenstack (i.e. the one which called this
    function).

``greenstack.GreenstackExit``
    This special exception does not propagate to the parent greenstack; it
    can be used to kill a single greenstack.

The ``greenstack`` type can be subclassed, too.  A greenstack runs by calling
its ``run`` attribute, which is normally set when the greenstack is
created; but for subclasses it also makes sense to define a ``run`` method
instead of giving a ``run`` argument to the constructor.

Switching
---------

Switches between greenstacks occur when the method switch() of a greenstack is
called, in which case execution jumps to the greenstack whose switch() is
called, or when a greenstack dies, in which case execution jumps to the
parent greenstack.  During a switch, an object or an exception is "sent" to
the target greenstack; this can be used as a convenient way to pass
information between greenstacks.  For example::

    def test1(x, y):
        z = gr2.switch(x+y)
        print z

    def test2(u):
        print u
        gr1.switch(42)

    gr1 = greenstack(test1)
    gr2 = greenstack(test2)
    gr1.switch("hello", " world")

This prints "hello world" and 42, with the same order of execution as the
previous example.  Note that the arguments of test1() and test2() are not
provided when the greenstack is created, but only the first time someone
switches to it.

Here are the precise rules for sending objects around:

``g.switch(*args, **kwargs)``
    Switches execution to the greenstack ``g``, sending it the given arguments.
    As a special case, if ``g`` did not start yet, then it will start to run
    now.

Dying greenstack
    If a greenstack's ``run()`` finishes, its return value is the object sent to
    its parent.  If ``run()`` terminates with an exception, the exception is
    propagated to its parent (unless it is a ``greenstack.GreenstackExit``
    exception, in which case the exception object is caught and *returned* to
    the parent).

Apart from the cases described above, the target greenstack normally
receives the object as the return value of the call to ``switch()`` in
which it was previously suspended.  Indeed, although a call to
``switch()`` does not return immediately, it will still return at some
point in the future, when some other greenstack switches back.  When this
occurs, then execution resumes just after the ``switch()`` where it was
suspended, and the ``switch()`` itself appears to return the object that
was just sent.  This means that ``x = g.switch(y)`` will send the object
``y`` to ``g``, and will later put the (unrelated) object that some
(unrelated) greenstack passes back to us into ``x``.

Note that any attempt to switch to a dead greenstack actually goes to the
dead greenstack's parent, or its parent's parent, and so on.  (The final
parent is the "main" greenstack, which is never dead.)

Methods and attributes of greenstacks
-----------------------------------

``g.switch(*args, **kwargs)``
    Switches execution to the greenstack ``g``.  See above.

``g.run``
    The callable that ``g`` will run when it starts.  After ``g`` started,
    this attribute no longer exists.

``g.parent``
    The parent greenstack.  This is writeable, but it is not allowed to
    create cycles of parents.

``g.gr_frame``
    The current top frame, or None.

``g.dead``
    True if ``g`` is dead (i.e. it finished its execution).

``bool(g)``
    True if ``g`` is active, False if it is dead or not yet started.

``g.throw([typ, [val, [tb]]])``
    Switches execution to the greenstack ``g``, but immediately raises the
    given exception in ``g``.  If no argument is provided, the exception
    defaults to ``greenstack.GreenstackExit``.  The normal exception
    propagation rules apply, as described above.  Note that calling this
    method is almost equivalent to the following::

        def raiser():
            raise typ, val, tb
        g_raiser = greenstack(raiser, parent=g)
        g_raiser.switch()

    except that this trick does not work for the
    ``greenstack.GreenstackExit`` exception, which would not propagate
    from ``g_raiser`` to ``g``.

Greenstacks and Python threads
----------------------------

Greenstacks can be combined with Python threads; in this case, each thread
contains an independent "main" greenstack with a tree of sub-greenstacks.  It
is not possible to mix or switch between greenstacks belonging to different
threads.

Garbage-collecting live greenstacks
---------------------------------

If all the references to a greenstack object go away (including the
references from the parent attribute of other greenstacks), then there is no
way to ever switch back to this greenstack.  In this case, a GreenstackExit
exception is generated into the greenstack.  This is the only case where a
greenstack receives the execution asynchronously.  This gives
``try:finally:`` blocks a chance to clean up resources held by the
greenstack.  This feature also enables a programming style in which
greenstacks are infinite loops waiting for data and processing it.  Such
loops are automatically interrupted when the last reference to the
greenstack goes away.

The greenstack is expected to either die or be resurrected by having a new
reference to it stored somewhere; just catching and ignoring the
GreenstackExit is likely to lead to an infinite loop.

Greenstacks do not participate in garbage collection; cycles involving data
that is present in a greenstack's frames will not be detected.  Storing
references to other greenstacks cyclically may lead to leaks.

Tracing support
---------------

Standard Python tracing and profiling doesn't work as expected when used with
Greenstack since stack and frame switching happens on the same Python thread.
It is difficult to detect greenstack switching reliably with conventional
methods, so to improve support for debugging, tracing and profiling greenstack
based code there are new functions in the greenstack module:

``greenstack.gettrace()``
    Returns a previously set tracing function, or None.

``greenstack.settrace(callback)``
    Sets a new tracing function and returns a previous tracing function, or
    None. The callback is called on various events and is expected to have
    the following signature::

        def callback(event, args):
            if event == 'switch':
                origin, target = args
                # Handle a switch from origin to target.
                # Note that callback is running in the context of target
                # greenstack and any exceptions will be passed as if
                # target.throw() was used instead of a switch.
                return
            if event == 'throw':
                origin, target = args
                # Handle a throw from origin to target.
                # Note that callback is running in the context of target
                # greenstack and any exceptions will replace the original, as
                # if target.throw() was used with the replacing exception.
                return

    For compatibility it is very important to unpack args tuple only when
    event is either ``'switch'`` or ``'throw'`` and not when ``event`` is
    potentially something else. This way API can be extended to new events
    similar to ``sys.settrace()``.

Custom state handlers
---------------------

If you're writing a C extension with some thread-local state that you'd like to
be greenstack-local, you can use the state handler API to add custom code to the
switching process to save and restore it. The API requires you to specify two
functions. The first one is the switch wrapper, which should save all state
into local variables, call ``PyGreenstack_CALL_NEXT`` with the parameter, and
restore state from the local variables. ::

    // spam is a global variable
    void spam_switchwrapper(void *next) {
        int local_spam = spam;
        PyGreenstack_CALL_NEXT(next);
        spam = local_spam;
    }

The second is the init state function, which should initialize the global state
to what it should be in a newly created greenstack. ::

    void spam_initstate() {
        spam = 0;
    }

You then need to register these functions using ``PyGreenstack_AddStateHandler``::

    PyGreenstack_AddStateHandler(spam_switchwrapper, spam_initstate);

And you're done!

C API Reference
===============

Greenstacks can be created and manipulated from extension modules written in C or
C++, or from applications that embed Python. The ``greenstack.h`` header is
provided, and exposes the entire API available to pure Python modules.

Types
-----
+-----------------+-----------------------+
| Type name       | Python name           |
+=================+=======================+
| PyGreenstack    | greenstack.greenstack |
+-----------------+-----------------------+

Exceptions
----------
+-----------------------+---------------------------+
| Type name             | Python name               |
+=======================+===========================+
| PyExc_GreenstackError | greenstack.error          |
+-----------------------+---------------------------+
| PyExc_GreenstackExit  | greenstack.GreenstackExit |
+-----------------------+---------------------------+

Reference
---------

``PyGreenstack_Import()``
    A macro that imports the greenstack module and initializes the C API. This
    must be called once for each extension module that uses the greenstack C API.

``int PyGreenstack_Check(PyObject *p)``
    Macro that returns true if the argument is a PyGreenstack.

``int PyGreenstack_STARTED(PyGreenstack *g)``
    Macro that returns true if the greenstack ``g`` has started.

``int PyGreenstack_ACTIVE(PyGreenstack *g)``
    Macro that returns true if the greenstack ``g`` has started and has not died.

``PyGreenstack *PyGreenstack_GET_PARENT(PyGreenstack *g)``
    Macro that returns the parent greenstack of ``g``.

``int PyGreenstack_SetParent(PyGreenstack *g, PyGreenstack *nparent)``
    Set the parent greenstack of ``g``. Returns 0 for success. If -1 is returned,
    then ``g`` is not a pointer to a PyGreenstack, and an AttributeError will
    be raised.

``PyGreenstack *PyGreenstack_GetCurrent(void)``
    Returns the currently active greenstack object.

``PyGreenstack *PyGreenstack_New(PyObject *run, PyObject *parent)``
    Creates a new greenstack object with the callable ``run`` and parent
    ``parent``. Both parameters are optional. If ``run`` is NULL, then the
    greenstack will be created, but will fail if switched in. If ``parent`` is
    NULL, the parent is automatically set to the current greenstack.

``PyObject *PyGreenstack_Switch(PyGreenstack *g, PyObject *args, PyObject *kwargs)``
    Switches to the greenstack ``g``. ``args`` and ``kwargs`` are optional and
    can be NULL. If ``args`` is NULL, an empty tuple is passed to the target
    greenstack. If kwargs is NULL, no keyword arguments are passed to the target
    greenstack. If arguments are specified, ``args`` should be a tuple and
    ``kwargs`` should be a dict.

``PyObject *PyGreenstack_Throw(PyGreenstack *g, PyObject *typ, PyObject *val, PyObject *tb)``
    Switches to greenstack ``g``, but immediately raise an exception of type
    ``typ`` with the value ``val``, and optionally, the traceback object
    ``tb``. ``tb`` can be NULL.

``int PyGreenstack_AddStateHandler(switchwrapperfunc wrapper, stateinitfunc stateinit)``
    Adds a state handler with the two specified functions. There is currently
    no API to remove state handlers.

Indices and tables
==================

* :ref:`search`
