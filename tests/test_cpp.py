import greenstack
import _test_extension_cpp

def test_exception_switch():
    greenlets = []
    for i in range(4):
        g = greenstack.greenlet(_test_extension_cpp.test_exception_switch)
        g.switch(i)
        greenlets.append(g)
    for i, g in enumerate(greenlets):
        assert g.switch() == i
