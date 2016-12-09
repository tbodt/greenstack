import gc
import sys
import unittest
import weakref

import greenstack

class GCTests(unittest.TestCase):
    def test_dead_circular_ref(self):
        o = weakref.ref(greenstack.greenstack(greenstack.getcurrent).switch())
        gc.collect()
        self.assertTrue(o() is None)
        self.assertFalse(gc.garbage, gc.garbage)

        # These only work with greenstack gc support

        def test_circular_greenstack(self):
            class circular_greenstack(greenstack.greenstack):
                pass
            o.self = o
            o = weakref.ref(o)
            gc.collect()
            self.assertTrue(o() is None)
            self.assertFalse(gc.garbage, gc.garbage)

        def test_inactive_ref(self):
            class inactive_greenstack(greenstack.greenstack):
                def __init__(self):
                    greenstack.greenstack.__init__(self, run=self.run)

                def run(self):
                    pass
            o = inactive_greenstack()
            o = weakref.ref(o)
            gc.collect()
            self.assertTrue(o() is None)
            self.assertFalse(gc.garbage, gc.garbage)

        def test_finalizer_crash(self):
            # This test is designed to crash when active greenstacks
            # are made garbage collectable, until the underlying
            # problem is resolved. How does it work:
            # - order of object creation is important
            # - array is created first, so it is moved to unreachable first
            # - we create a cycle between a greenstack and this array
            # - we create an object that participates in gc, is only
            #   referenced by a greenstack, and would corrupt gc lists
            #   on destruction, the easiest is to use an object with
            #   a finalizer
            # - because array is the first object in unreachable it is
            #   cleared first, which causes all references to greenstack
            #   to disappear and causes greenstack to be destroyed, but since
            #   it is still live it causes a switch during gc, which causes
            #   an object with finalizer to be destroyed, which causes stack
            #   corruption and then a crash
            class object_with_finalizer(object):
                def __del__(self):
                    pass
            array = []
            parent = greenstack.getcurrent()
            def greenstack_body():
                greenstack.getcurrent().object = object_with_finalizer()
                try:
                    parent.switch()
                finally:
                    del greenstack.getcurrent().object
            g = greenstack.greenstack(greenstack_body)
            g.array = array
            array.append(g)
            g.switch()
            del array
            del g
            greenstack.getcurrent()
            gc.collect()
