import unittest

import greenstack
import _test_extension_cpp


class CPPTests(unittest.TestCase):
    def test_exception_switch(self):
        greenlets = []
        for i in range(4):
            g = greenstack.greenlet(_test_extension_cpp.test_exception_switch)
            g.switch(i)
            greenlets.append(g)
        for i, g in enumerate(greenlets):
            self.assertEqual(g.switch(), i)
