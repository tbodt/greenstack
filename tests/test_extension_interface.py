import sys

import greenstack
import _test_extension
import pytest

def test_switch():
    assert 50 == _test_extension.test_switch(greenstack.greenstack(lambda: 50))

def test_switch_kwargs():
    def foo(x, y):
        return x * y
    g = greenstack.greenstack(foo)
    assert 6 == _test_extension.test_switch_kwargs(g, x=3, y=2)

def test_setparent():
    def foo():
        def bar():
            greenstack.getcurrent().parent.switch()

            # This final switch should go back to the main greenstack, since
            # the test_setparent() function in the C extension should have
            # reparented this greenstack.
            greenstack.getcurrent().parent.switch()
            raise AssertionError("Should never have reached this code")
        child = greenstack.greenstack(bar)
        child.switch()
        greenstack.getcurrent().parent.switch(child)
        greenstack.getcurrent().parent.throw(
            AssertionError("Should never reach this code"))
    foo_child = greenstack.greenstack(foo).switch()
    assert None == _test_extension.test_setparent(foo_child)

def test_getcurrent():
    _test_extension.test_getcurrent()

def test_new_greenstack():
    assert -15 == _test_extension.test_new_greenstack(lambda: -15)

def test_raise_greenstack_dead():
    with pytest.raises(
        greenstack.GreenstackExit):
        _test_extension.test_raise_dead_greenstack()

def test_raise_greenstack_error():
    with pytest.raises(
        greenstack.error):
        _test_extension.test_raise_greenstack_error()

def test_throw():
    seen = []

    def foo():
        try:
            greenstack.getcurrent().parent.switch()
        except ValueError:
            seen.append(sys.exc_info()[1])
        except greenstack.GreenstackExit:
            raise AssertionError
    g = greenstack.greenstack(foo)
    g.switch()
    _test_extension.test_throw(g)
    assert len(seen) == 1
    assert isinstance(seen[0], ValueError), \
        "ValueError was not raised in foo()"
    assert str(seen[0]) == \
        'take that sucka!', \
        "message doesn't match"
