import gc
import sys
import time
import threading
import unittest

from greenstack import greenstack

try:
    from abc import ABCMeta, abstractmethod
except ImportError:
    ABCMeta = None
    abstractmethod = None


class SomeError(Exception):
    pass


def fmain(seen):
    try:
        greenstack.getcurrent().parent.switch()
    except:
        seen.append(sys.exc_info()[0])
        raise
    raise SomeError


def send_exception(g, exc):
    # note: send_exception(g, exc)  can be now done with  g.throw(exc).
    # the purpose of this test is to explicitely check the propagation rules.
    def crasher(exc):
        raise exc
    g1 = greenstack(crasher, parent=g)
    g1.switch(exc)


class GreenstackTests(unittest.TestCase):
    def test_simple(self):
        lst = []

        def f():
            lst.append(1)
            greenstack.getcurrent().parent.switch()
            lst.append(3)
        g = greenstack(f)
        lst.append(0)
        g.switch()
        lst.append(2)
        g.switch()
        lst.append(4)
        self.assertEqual(lst, list(range(5)))

    def test_parent_equals_None(self):
        g = greenstack(parent=None)

    def test_run_equals_None(self):
        g = greenstack(run=None)

    def test_two_children(self):
        lst = []

        def f():
            lst.append(1)
            greenstack.getcurrent().parent.switch()
            lst.extend([1, 1])
        g = greenstack(f)
        h = greenstack(f)
        g.switch()
        self.assertEqual(len(lst), 1)
        h.switch()
        self.assertEqual(len(lst), 2)
        h.switch()
        self.assertEqual(len(lst), 4)
        self.assertEqual(h.dead, True)
        g.switch()
        self.assertEqual(len(lst), 6)
        self.assertEqual(g.dead, True)

    def test_two_recursive_children(self):
        lst = []

        def f():
            lst.append(1)
            greenstack.getcurrent().parent.switch()

        def g():
            lst.append(1)
            g = greenstack(f)
            g.switch()
            lst.append(1)
        g = greenstack(g)
        g.switch()
        self.assertEqual(len(lst), 3)
        self.assertEqual(sys.getrefcount(g), 2)

    def test_threads(self):
        success = []

        def f():
            self.test_simple()
            success.append(True)
        ths = [threading.Thread(target=f) for i in range(10)]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
        self.assertEqual(len(success), len(ths))

    def test_exception(self):
        seen = []
        g1 = greenstack(fmain)
        g2 = greenstack(fmain)
        g1.switch(seen)
        g2.switch(seen)
        g2.parent = g1
        self.assertEqual(seen, [])
        self.assertRaises(SomeError, g2.switch)
        self.assertEqual(seen, [SomeError])
        g2.switch()
        self.assertEqual(seen, [SomeError])

    def test_send_exception(self):
        seen = []
        g1 = greenstack(fmain)
        g1.switch(seen)
        self.assertRaises(KeyError, send_exception, g1, KeyError)
        self.assertEqual(seen, [KeyError])

    def test_dealloc(self):
        seen = []
        g1 = greenstack(fmain)
        g2 = greenstack(fmain)
        g1.switch(seen)
        g2.switch(seen)
        self.assertEqual(seen, [])
        del g1
        gc.collect()
        self.assertEqual(seen, [greenstack.GreenstackExit])
        del g2
        gc.collect()
        self.assertEqual(seen, [greenstack.GreenstackExit, greenstack.GreenstackExit])

    def test_dealloc_other_thread(self):
        seen = []
        someref = []
        lock = threading.Lock()
        lock.acquire()
        lock2 = threading.Lock()
        lock2.acquire()

        def f():
            g1 = greenstack(fmain)
            g1.switch(seen)
            someref.append(g1)
            del g1
            gc.collect()
            lock.release()
            lock2.acquire()
            greenstack()   # trigger release
            lock.release()
            lock2.acquire()
        t = threading.Thread(target=f)
        t.start()
        lock.acquire()
        self.assertEqual(seen, [])
        self.assertEqual(len(someref), 1)
        del someref[:]
        gc.collect()
        # g1 is not released immediately because it's from another thread
        self.assertEqual(seen, [])
        lock2.release()
        lock.acquire()
        self.assertEqual(seen, [greenstack.GreenstackExit])
        lock2.release()
        t.join()

    def test_frame(self):
        def f1():
            f = sys._getframe(0)
            self.assertEqual(f.f_back, None)
            greenstack.getcurrent().parent.switch(f)
            return "meaning of life"
        g = greenstack(f1)
        frame = g.switch()
        self.assertTrue(frame is g.gr_frame)
        self.assertTrue(g)
        next = g.switch()
        self.assertFalse(g)
        self.assertEqual(next, 'meaning of life')
        self.assertEqual(g.gr_frame, None)

    def test_thread_bug(self):
        def runner(x):
            g = greenstack(lambda: time.sleep(x))
            g.switch()
        t1 = threading.Thread(target=runner, args=(0.2,))
        t2 = threading.Thread(target=runner, args=(0.3,))
        t1.start()
        t2.start()
        t1.join()
        t2.join()

    def test_switch_kwargs(self):
        def foo(a, b):
            self.assertEqual(a, 4)
            self.assertEqual(b, 2)
        greenstack(foo).switch(a=4, b=2)

    def test_switch_kwargs_to_parent(self):
        def foo(x):
            greenstack.getcurrent().parent.switch(x=x)
            greenstack.getcurrent().parent.switch(2, x=3)
            return x, x ** 2
        g = greenstack(foo)
        self.assertEqual({'x': 3}, g.switch(3))
        self.assertEqual(((2,), {'x': 3}), g.switch())
        self.assertEqual((3, 9), g.switch())

    def test_switch_to_another_thread(self):
        data = {}
        error = None
        created_event = threading.Event()
        done_event = threading.Event()

        def foo():
            data['g'] = greenstack(lambda: None)
            created_event.set()
            done_event.wait()
        thread = threading.Thread(target=foo)
        thread.start()
        created_event.wait()
        try:
            data['g'].switch()
        except greenstack.error:
            error = sys.exc_info()[1]
        self.assertTrue(error != None, "greenstack.error was not raised!")
        done_event.set()
        thread.join()

    def test_exc_state(self):
        def f():
            try:
                raise ValueError('fun')
            except:
                exc_info = sys.exc_info()
                greenstack(h).switch()
                self.assertEqual(exc_info, sys.exc_info())

        def h():
            self.assertEqual(sys.exc_info(), (None, None, None))

        greenstack(f).switch()

    def test_instance_dict(self):
        def f():
            greenstack.getcurrent().test = 42
        def deldict(g):
            del g.__dict__
        def setdict(g, value):
            g.__dict__ = value
        g = greenstack(f)
        self.assertEqual(g.__dict__, {})
        g.switch()
        self.assertEqual(g.test, 42)
        self.assertEqual(g.__dict__, {'test': 42})
        g.__dict__ = g.__dict__
        self.assertEqual(g.__dict__, {'test': 42})
        self.assertRaises(TypeError, deldict, g)
        self.assertRaises(TypeError, setdict, g, 42)

    def test_threaded_reparent(self):
        data = {}
        created_event = threading.Event()
        done_event = threading.Event()

        def foo():
            data['g'] = greenstack(lambda: None)
            created_event.set()
            done_event.wait()

        def blank():
            greenstack.getcurrent().parent.switch()

        def setparent(g, value):
            g.parent = value

        thread = threading.Thread(target=foo)
        thread.start()
        created_event.wait()
        g = greenstack(blank)
        g.switch()
        self.assertRaises(ValueError, setparent, g, data['g'])
        done_event.set()
        thread.join()

    def test_deepcopy(self):
        import copy
        self.assertRaises(TypeError, copy.copy, greenstack())
        self.assertRaises(TypeError, copy.deepcopy, greenstack())

    def test_parent_restored_on_kill(self):
        hub = greenstack(lambda: None)
        main = greenstack.getcurrent()
        result = []
        def worker():
            try:
                # Wait to be killed
                main.switch()
            except greenstack.GreenstackExit:
                # Resurrect and switch to parent
                result.append(greenstack.getcurrent().parent)
                result.append(greenstack.getcurrent())
                hub.switch()
        g = greenstack(worker, parent=hub)
        g.switch()
        del g
        self.assertTrue(result)
        self.assertEqual(result[0], main)
        self.assertEqual(result[1].parent, hub)

    def test_parent_return_failure(self):
        # No run causes AttributeError on switch
        g1 = greenstack()
        # Greenstack that implicitly switches to parent
        g2 = greenstack(lambda: None, parent=g1)
        # AttributeError should propagate to us, no fatal errors
        self.assertRaises(AttributeError, g2.switch)

    def test_throw_exception_not_lost(self):
        class mygreenstack(greenstack):
            def __getattribute__(self, name):
                try:
                    raise Exception()
                except:
                    pass
                return greenstack.__getattribute__(self, name)
        g = mygreenstack(lambda: None)
        self.assertRaises(SomeError, g.throw, SomeError())

    def test_throw_doesnt_crash(self):
        result = []
        def worker():
            greenstack.getcurrent().parent.switch()
        def creator():
            g = greenstack(worker)
            g.switch()
            result.append(g)
        t = threading.Thread(target=creator)
        t.start()
        t.join()
        self.assertRaises(greenstack.error, result[0].throw, SomeError())

    def test_recursive_startup(self):
        class convoluted(greenstack):
            def __init__(self):
                greenstack.__init__(self)
                self.count = 0
            def __getattribute__(self, name):
                if name == 'run' and self.count == 0:
                    self.count = 1
                    self.switch(43)
                return greenstack.__getattribute__(self, name)
            def run(self, value):
                while True:
                    self.parent.switch(value)
        g = convoluted()
        self.assertEqual(g.switch(42), 43)

    def test_unexpected_reparenting(self):
        another = []
        def worker():
            g = greenstack(lambda: None)
            another.append(g)
            g.switch()
        t = threading.Thread(target=worker)
        t.start()
        t.join()
        class convoluted(greenstack):
            def __getattribute__(self, name):
                if name == 'run':
                    self.parent = another[0]
                return greenstack.__getattribute__(self, name)
        g = convoluted(lambda: None)
        self.assertRaises(greenstack.error, g.switch)

    def test_threaded_updatecurrent(self):
        # released when main thread should execute
        lock1 = threading.Lock()
        lock1.acquire()
        # released when another thread should execute
        lock2 = threading.Lock()
        lock2.acquire()
        class finalized(object):
            def __del__(self):
                # happens while in green_updatecurrent() in main greenstack
                # should be very careful not to accidentally call it again
                # at the same time we must make sure another thread executes
                lock2.release()
                lock1.acquire()
                # now ts_current belongs to another thread
        def deallocator():
            greenstack.getcurrent().parent.switch()
        def fthread():
            lock2.acquire()
            greenstack.getcurrent()
            del g[0]
            lock1.release()
            lock2.acquire()
            greenstack.getcurrent()
            lock1.release()
        main = greenstack.getcurrent()
        g = [greenstack(deallocator)]
        g[0].bomb = finalized()
        g[0].switch()
        t = threading.Thread(target=fthread)
        t.start()
        # let another thread grab ts_current and deallocate g[0]
        lock2.release()
        lock1.acquire()
        # this is the corner stone
        # getcurrent() will notice that ts_current belongs to another thread
        # and start the update process, which would notice that g[0] should
        # be deallocated, and that will execute an object's finalizer. Now,
        # that object will let another thread run so it can grab ts_current
        # again, which would likely crash the interpreter if there's no
        # check for this case at the end of green_updatecurrent(). This test
        # passes if getcurrent() returns correct result, but it's likely
        # to randomly crash if it's not anyway.
        self.assertEqual(greenstack.getcurrent(), main)
        # wait for another thread to complete, just in case
        t.join()

    def test_dealloc_switch_args_not_lost(self):
        seen = []
        def worker():
            # wait for the value
            value = greenstack.getcurrent().parent.switch()
            # delete all references to ourself
            del worker[0]
            initiator.parent = greenstack.getcurrent().parent
            # switch to main with the value, but because
            # ts_current is the last reference to us we
            # return immediately
            try:
                greenstack.getcurrent().parent.switch(value)
            finally:
                seen.append(greenstack.getcurrent())
        def initiator():
            return 42 # implicitly falls thru to parent
        worker = [greenstack(worker)]
        worker[0].switch() # prime worker
        initiator = greenstack(initiator, worker[0])
        value = initiator.switch()
        self.assertTrue(seen)
        self.assertEqual(value, 42)

    if sys.version_info[0] == 2:
        # There's no apply in Python 3.x
        def test_tuple_subclass(self):
            class mytuple(tuple):
                def __len__(self):
                    greenstack.getcurrent().switch()
                    return tuple.__len__(self)
            args = mytuple()
            kwargs = dict(a=42)
            def switchapply():
                apply(greenstack.getcurrent().parent.switch, args, kwargs)
            g = greenstack(switchapply)
            self.assertEqual(g.switch(), kwargs)

    if ABCMeta is not None and abstractmethod is not None:
        def test_abstract_subclasses(self):
            AbstractSubclass = ABCMeta(
                'AbstractSubclass',
                (greenstack,),
                {'run': abstractmethod(lambda self: None)})

            class BadSubclass(AbstractSubclass):
                pass

            class GoodSubclass(AbstractSubclass):
                def run(self):
                    pass

            GoodSubclass() # should not raise
            self.assertRaises(TypeError, BadSubclass)

    def test_implicit_parent_with_threads(self):
        if not gc.isenabled():
            return # cannot test with disabled gc
        N = gc.get_threshold()[0]
        if N < 50:
            return # cannot test with such a small N
        def attempt():
            lock1 = threading.Lock()
            lock1.acquire()
            lock2 = threading.Lock()
            lock2.acquire()
            recycled = [False]
            def another_thread():
                lock1.acquire() # wait for gc
                greenstack.getcurrent() # update ts_current
                lock2.release() # release gc
            t = threading.Thread(target=another_thread)
            t.start()
            class gc_callback(object):
                def __del__(self):
                    lock1.release()
                    lock2.acquire()
                    recycled[0] = True
            class garbage(object):
                def __init__(self):
                    self.cycle = self
                    self.callback = gc_callback()
            l = []
            x = range(N*2)
            current = greenstack.getcurrent()
            g = garbage()
            for i in x:
                g = None # lose reference to garbage
                if recycled[0]:
                    # gc callback called prematurely
                    t.join()
                    return False
                last = greenstack()
                if recycled[0]:
                    break # yes! gc called in green_new
                l.append(last) # increase allocation counter
            else:
                # gc callback not called when expected
                gc.collect()
                if recycled[0]:
                    t.join()
                return False
            self.assertEqual(last.parent, current)
            for g in l:
                self.assertEqual(g.parent, current)
            return True
        for i in range(5):
            if attempt():
                break
