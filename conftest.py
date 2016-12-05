# configuration file for py.test

import sys, os
from distutils.spawn import spawn


def pytest_configure(config):
    os.chdir(os.path.dirname(__file__))
    cmd = [sys.executable, "setup.py", "-q", "build_ext", "-q", "-i"]
    spawn(cmd, search_path=0)

    from tests import build_test_extensions
    build_test_extensions()


def pytest_report_header(config):
    import greenstack
    return "greenstack %s from %s" % (greenstack.__version__, greenstack.__file__)
