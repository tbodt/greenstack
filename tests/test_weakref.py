import gc
import greenstack
import weakref
import pytest

def test_dead_weakref():
    def _dead_greenstack():
        g = greenstack.greenstack(lambda: None)
        g.switch()
        return g
    o = weakref.ref(_dead_greenstack())
    gc.collect()
    assert o() == None

def test_inactive_weakref():
    o = weakref.ref(greenstack.greenstack())
    gc.collect()
    assert o() == None

def test_dealloc_weakref():
    seen = []
    def worker():
        try:
            greenstack.getcurrent().parent.switch()
        finally:
            seen.append(g())
    g = greenstack.greenstack(worker)
    g.switch()
    g2 = greenstack.greenstack(lambda: None, g)
    g = weakref.ref(g2)
    g2 = None
    assert seen == [None]
