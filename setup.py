#! /usr/bin/env python

import sys, os, glob, platform, tempfile, shutil

try:
    if not (sys.modules.get("setuptools")
            or "develop" in sys.argv
            or "upload" in sys.argv
            or "bdist_egg" in sys.argv
            or "bdist_wheel" in sys.argv
            or "test" in sys.argv):
        raise ImportError()
    from setuptools import setup, Extension
    setuptools_args = dict(test_suite='tests.test_collector', zip_safe=False)
except ImportError:
    from distutils.core import setup, Extension
    setuptools_args = dict()

def readfile(filename):
    f = open(filename)
    try:
        return f.read()
    finally:
        f.close()

if hasattr(sys, "pypy_version_info"):
    ext_modules = []
    headers = []
else:
    headers = ['greenstack.h']

    if sys.platform == 'win32' and os.environ.get('GREENSTACK_STATIC_RUNTIME') in ('1', 'yes'):
        extra_compile_args = ['/MT']
    else:
        extra_compile_args = []

    ext_modules = [Extension(
        name='greenstack',
        sources=['greenstack.c', 'libcoro/coro.c'],
        extra_compile_args=extra_compile_args,
        depends=['greenstack.h', 'libcoro/coro.h'])]

from distutils.core import Command
from my_build_ext import build_ext


setup(
    name="greenstack",
    version='0.6',
    description='Lightweight in-process concurrent programming',
    long_description=readfile("README.rst"),
    maintainer="Theodore Dubois",
    maintainer_email="tblodt@icloud.com",
    url="https://github.com/tbodt/greenstack",
    license="MIT License",
    platforms=['any'],
    headers=headers,
    ext_modules=ext_modules,
    cmdclass=dict(build_ext=build_ext),
    classifiers=[
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Natural Language :: English',
        'Programming Language :: C',
        'Programming Language :: Python',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.4',
        'Programming Language :: Python :: 2.5',
        'Programming Language :: Python :: 2.6',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.0',
        'Programming Language :: Python :: 3.1',
        'Programming Language :: Python :: 3.2',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
        'Operating System :: OS Independent',
        'Topic :: Software Development :: Libraries :: Python Modules'],
    **setuptools_args)
