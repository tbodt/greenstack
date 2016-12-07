import threading
import greenstack
import pytest

class SomeError(Exception):
    pass

if greenstack.GREENSTACK_USE_TRACING:
    def test_greenstack_tracing():
        main = greenstack.getcurrent()
        actions = []
        def trace(*args):
            actions.append(args)
        def dummy():
            pass
        def dummyexc():
            raise SomeError()
        oldtrace = greenstack.settrace(trace)
        try:
            g1 = greenstack.greenstack(dummy)
            g1.switch()
            g2 = greenstack.greenstack(dummyexc)
            with pytest.raises(SomeError):
                g2.switch()
        finally:
            greenstack.settrace(oldtrace)
        assert actions == [
            ('switch', (main, g1)),
            ('switch', (g1, main)),
            ('switch', (main, g2)),
            ('throw', (g2, main)),
        ]

    def test_exception_disables_tracing():
        main = greenstack.getcurrent()
        actions = []
        def trace(*args):
            actions.append(args)
            raise SomeError()
        def dummy():
            main.switch()
        g = greenstack.greenstack(dummy)
        g.switch()
        oldtrace = greenstack.settrace(trace)
        try:
            with pytest.raises(SomeError):
                g.switch()
            assert greenstack.gettrace() == None
        finally:
            greenstack.settrace(oldtrace)
        assert actions == [
            ('switch', (main, g)),
        ]
