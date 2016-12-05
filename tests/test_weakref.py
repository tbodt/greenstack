import gc
import greenstack
import weakref
import unittest


class WeakRefTests(unittest.TestCase):
    def test_dead_weakref(self):
        def _dead_greenlet():
            g = greenstack.greenlet(lambda: None)
            g.switch()
            return g
        o = weakref.ref(_dead_greenlet())
        gc.collect()
        self.assertEqual(o(), None)

    def test_inactive_weakref(self):
        o = weakref.ref(greenstack.greenlet())
        gc.collect()
        self.assertEqual(o(), None)

    def test_dealloc_weakref(self):
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
        self.assertEqual(seen, [None])
