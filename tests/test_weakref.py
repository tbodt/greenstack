import gc
import greenstack
import weakref
import pytest

def test_dead_weakref():
    def _dead_greenlet():
        g = greenstack.greenlet(lambda: None)
        g.switch()
        return g
    o = weakref.ref(_dead_greenlet())
    gc.collect()
    assert o() == None

def test_inactive_weakref():
    o = weakref.ref(greenstack.greenlet())
    gc.collect()
    assert o() == None

def test_dealloc_weakref():
    seen = []
    def worker():
        try:
            greenstack.getcurrent().parent.switch()
        finally:
            seen.append(g())
    g = greenstack.greenlet(worker)
    g.switch()
    g2 = greenstack.greenlet(lambda: None, g)
    g = weakref.ref(g2)
    g2 = None
    assert seen == [None]
