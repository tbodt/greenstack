import greenstack
import _test_extension_cpp

def test_exception_switch():
    greenstacks = []
    for i in range(4):
        g = greenstack.greenstack(_test_extension_cpp.test_exception_switch)
        g.switch(i)
        greenstacks.append(g)
    for i, g in enumerate(greenstacks):
        assert g.switch() == i
