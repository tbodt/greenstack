#!/usr/bin/env python
from setuptools import setup

setup(
    name='greenstack-greenlet',
    version='1.0',
    description='Greenlet compatibility for Greenstack',
    long_description=open('README.rst').read(),
    author='Theodore Dubois',
    author_email='tblodt@icloud.com',
    url='https://github.com/tbodt/greenstack/tree/master/greenstack-greenlet',
    install_requires=['greenstack'],
    license='MIT License',
    platforms=['any'],
    classifiers=[
        'License :: OSI Approved :: MIT License',
        'Natural Language :: English',
        'Programming Language :: Python',
        'Operating System :: OS Independent',
        'Topic :: Software Development :: Libraries :: Python Modules'
    ],
)
