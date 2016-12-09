import gc
import greenstack
import weakref
import unittest


class WeakRefTests(unittest.TestCase):
    def test_dead_weakref(self):
        def _dead_greenstack():
            g = greenstack.greenstack(lambda: None)
            g.switch()
            return g
        o = weakref.ref(_dead_greenstack())
        gc.collect()
        self.assertEqual(o(), None)

    def test_inactive_weakref(self):
        o = weakref.ref(greenstack.greenstack())
        gc.collect()
        self.assertEqual(o(), None)

    def test_dealloc_weakref(self):
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
        self.assertEqual(seen, [None])
