#!/usr/bin/env python

"""Create a chain of coroutines and pass a value from one end to the other,
where each coroutine will increment the value before passing it along.
"""

import optparse
import time

import greenstack


def link(next_greenstack):
    value = greenstack.getcurrent().parent.switch()
    next_greenstack.switch(value + 1)


def chain(n):
    start_node = greenstack.getcurrent()
    for i in xrange(n):
        g = greenstack.greenstack(link)
        g.switch(start_node)
        start_node = g
    return start_node.switch(0)

if __name__ == '__main__':
    p = optparse.OptionParser(
        usage='%prog [-n NUM_COROUTINES]', description=__doc__)
    p.add_option(
        '-n', type='int', dest='num_greenstacks', default=100000,
        help='The number of greenstacks in the chain.')
    options, args = p.parse_args()

    if len(args) != 0:
        p.error('unexpected arguments: %s' % ', '.join(args))

    start_time = time.clock()
    print 'Result:', chain(options.num_greenstacks)
    print time.clock() - start_time, 'seconds'
