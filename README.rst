===============================================
Greenstack: Cooperative green threads in Python
===============================================

.. image:: https://travis-ci.org/tbodt/greenstack.svg?branch=master
    :target: https://travis-ci.org/tbodt/greenstack

Greenstack is a fork of greenlet, which is a spin-off of `Stackless`_, a
version of CPython that supports micro-threads called "tasklets".  Tasklets run
pseudo-concurrently (typically in a single or a few OS-level threads) and are
synchronized with data exchanges on "channels".

A "greenlet", on the other hand, is a still more primitive notion of
micro-thread with no implicit scheduling; coroutines, in other words. This is
useful when you want to control exactly when your code runs. You can build
custom scheduled micro-threads on top of greenlet; however, it seems that
greenlets are useful on their own as a way to make advanced control flow
structures. For example, we can recreate generators; the difference with
Python's own generators is that our generators can call nested functions and
the nested functions can yield values too. Additionally, you don't need a
"yield" keyword. See the example in tests/test_generator.py.  

Greenlets are provided as a C extension module for the regular unmodified
interpreter.

Greenlets are coroutines for in-process concurrent programming.

.. _`Stackless`: http://www.stackless.com

Getting Greenstack
==================

Currently, the only way to get Greenstack is by building it from source::

    git clone git@github.com:tbodt/greenstack
    cd greenstack
    ./setup.py install

This will change soon.
