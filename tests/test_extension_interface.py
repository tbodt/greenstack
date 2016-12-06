import sys

import greenstack
import _test_extension
import pytest

def test_switch():
    assert 50 == _test_extension.test_switch(greenstack.greenlet(lambda: 50))

def test_switch_kwargs():
    def foo(x, y):
        return x * y
    g = greenstack.greenlet(foo)
    assert 6 == _test_extension.test_switch_kwargs(g, x=3, y=2)

def test_setparent():
    def foo():
        def bar():
            greenstack.getcurrent().parent.switch()

            # This final switch should go back to the main greenlet, since
            # the test_setparent() function in the C extension should have
            # reparented this greenlet.
            greenstack.getcurrent().parent.switch()
            raise AssertionError("Should never have reached this code")
        child = greenstack.greenlet(bar)
        child.switch()
        greenstack.getcurrent().parent.switch(child)
        greenstack.getcurrent().parent.throw(
            AssertionError("Should never reach this code"))
    foo_child = greenstack.greenlet(foo).switch()
    assert None == _test_extension.test_setparent(foo_child)

def test_getcurrent():
    _test_extension.test_getcurrent()

def test_new_greenlet():
    assert -15 == _test_extension.test_new_greenlet(lambda: -15)

def test_raise_greenlet_dead():
    with pytest.raises(
        greenstack.GreenletExit):
        _test_extension.test_raise_dead_greenlet()

def test_raise_greenlet_error():
    with pytest.raises(
        greenstack.error):
        _test_extension.test_raise_greenlet_error()

def test_throw():
    seen = []

    def foo():
        try:
            greenstack.getcurrent().parent.switch()
        except ValueError:
            seen.append(sys.exc_info()[1])
        except greenstack.GreenletExit:
            raise AssertionError
    g = greenstack.greenlet(foo)
    g.switch()
    _test_extension.test_throw(g)
    assert len(seen) == 1
    assert isinstance(seen[0], ValueError), \
        "ValueError was not raised in foo()"
    assert str(seen[0]) == \
        'take that sucka!', \
        "message doesn't match"
