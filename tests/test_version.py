#! /usr/bin/env python

import sys, os, re, unittest, greenstack

def readclose(f):
    try:
        return f.read()
    finally:
        f.close()

def readfile(filename):
    return readclose(open(filename))

class VersionTests(unittest.TestCase):
    def test_version(self):
        upfile = lambda p: os.path.join(os.path.dirname(__file__), "..", p)
        hversion, = re.findall('GREENSTACK_VERSION "(.*)"', readfile(upfile("greenstack.h")))
        sversion = readclose(os.popen("%s %s --version" % (sys.executable, upfile("setup.py")))).strip()
        self.assertFalse(sversion != hversion)
        self.assertFalse(sversion != greenstack.__version__)
