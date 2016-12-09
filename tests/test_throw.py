import sys
import unittest

from greenstack import greenstack


def switch(*args):
    return greenstack.getcurrent().parent.switch(*args)


class ThrowTests(unittest.TestCase):
    def test_class(self):
        def f():
            try:
                switch("ok")
            except RuntimeError:
                switch("ok")
                return
            switch("fail")
        g = greenstack(f)
        res = g.switch()
        self.assertEqual(res, "ok")
        res = g.throw(RuntimeError)
        self.assertEqual(res, "ok")

    def test_val(self):
        def f():
            try:
                switch("ok")
            except RuntimeError:
                val = sys.exc_info()[1]
                if str(val) == "ciao":
                    switch("ok")
                    return
            switch("fail")

        g = greenstack(f)
        res = g.switch()
        self.assertEqual(res, "ok")
        res = g.throw(RuntimeError("ciao"))
        self.assertEqual(res, "ok")

        g = greenstack(f)
        res = g.switch()
        self.assertEqual(res, "ok")
        res = g.throw(RuntimeError, "ciao")
        self.assertEqual(res, "ok")

    def test_kill(self):
        def f():
            switch("ok")
            switch("fail")
        g = greenstack(f)
        res = g.switch()
        self.assertEqual(res, "ok")
        res = g.throw()
        self.assertTrue(isinstance(res, greenstack.GreenstackExit))
        self.assertTrue(g.dead)
        res = g.throw()    # immediately eaten by the already-dead greenstack
        self.assertTrue(isinstance(res, greenstack.GreenstackExit))

    def test_throw_goes_to_original_parent(self):
        main = greenstack.getcurrent()

        def f1():
            try:
                main.switch("f1 ready to catch")
            except IndexError:
                return "caught"
            else:
                return "normal exit"

        def f2():
            main.switch("from f2")

        g1 = greenstack(f1)
        g2 = greenstack(f2, parent=g1)
        self.assertRaises(IndexError, g2.throw, IndexError)
        self.assertTrue(g2.dead)
        self.assertTrue(g1.dead)

        g1 = greenstack(f1)
        g2 = greenstack(f2, parent=g1)
        res = g1.switch()
        self.assertEqual(res, "f1 ready to catch")
        res = g2.throw(IndexError)
        self.assertEqual(res, "caught")
        self.assertTrue(g2.dead)
        self.assertTrue(g1.dead)

        g1 = greenstack(f1)
        g2 = greenstack(f2, parent=g1)
        res = g1.switch()
        self.assertEqual(res, "f1 ready to catch")
        res = g2.switch()
        self.assertEqual(res, "from f2")
        res = g2.throw(IndexError)
        self.assertEqual(res, "caught")
        self.assertTrue(g2.dead)
        self.assertTrue(g1.dead)
