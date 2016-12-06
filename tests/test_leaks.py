import sys
import gc

import time
import weakref
import greenstack
import threading


def test_arg_refs():
    args = ('a', 'b', 'c')
    refcount_before = sys.getrefcount(args)
    g = greenstack.greenlet(
        lambda *args: greenstack.getcurrent().parent.switch(*args))
    for i in range(100):
        g.switch(*args)
    assert sys.getrefcount(args) == refcount_before

def test_kwarg_refs():
    kwargs = {}
    g = greenstack.greenlet(
        lambda **kwargs: greenstack.getcurrent().parent.switch(**kwargs))
    for i in range(100):
        g.switch(**kwargs)
    assert sys.getrefcount(kwargs) == 2

if greenstack.GREENSTACK_USE_GC:
    # These only work with greenlet gc support

    def recycle_threads():
        # By introducing a thread that does sleep we allow other threads,
        # that have triggered their __block condition, but did not have a
        # chance to deallocate their thread state yet, to finally do so.
        # The way it works is by requring a GIL switch (different thread),
        # which does a GIL release (sleep), which might do a GIL switch
        # to finished threads and allow them to clean up.
        def worker():
            time.sleep(0.001)
        t = threading.Thread(target=worker)
        t.start()
        time.sleep(0.001)
        t.join()

    def test_threaded_leak():
        gg = []
        def worker():
            # only main greenlet present
            gg.append(weakref.ref(greenstack.getcurrent()))
        for i in range(2):
            t = threading.Thread(target=worker)
            t.start()
            t.join()
            del t
        greenstack.getcurrent() # update ts_current
        recycle_threads()
        greenstack.getcurrent() # update ts_current
        gc.collect()
        greenstack.getcurrent() # update ts_current
        for g in gg:
            assert g() is None

    def test_threaded_adv_leak():
        gg = []
        def worker():
            # main and additional *finished* greenlets
            ll = greenstack.getcurrent().ll = []
            def additional():
                ll.append(greenstack.getcurrent())
            for i in range(2):
                greenstack.greenlet(additional).switch()
            gg.append(weakref.ref(greenstack.getcurrent()))
        for i in range(2):
            t = threading.Thread(target=worker)
            t.start()
            t.join()
            del t
        greenstack.getcurrent() # update ts_current
        recycle_threads()
        greenstack.getcurrent() # update ts_current
        gc.collect()
        greenstack.getcurrent() # update ts_current
        for g in gg:
            assert g() is None
