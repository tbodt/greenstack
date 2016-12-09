import unittest

import greenstack
import _test_extension_cpp

class CPPTests(unittest.TestCase):
    def test_exception_switch(self):
        greenstacks = []
        for i in range(4):
            g = greenstack.greenstack(_test_extension_cpp.test_exception_switch)
            g.switch(i)
            greenstacks.append(g)
        for i, g in enumerate(greenstacks):
            self.assertEqual(g.switch(), i)
