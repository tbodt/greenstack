#!/usr/bin/env python

"""Create two greenstacks and switch back and forth between them a large number
of times.
"""

import optparse
import time

import greenstack

def switcher(n):
    while True:
        if greenstack.getcurrent() == switcher1:
            n = switcher2.switch(n)
        else:
            n = switcher1.switch(n)
        n -= 1
        if n == 0:
            return

if __name__ == '__main__':
    p = optparse.OptionParser(
        usage='%prog [-n NUM_BOUNCES]', description=__doc__)
    p.add_option(
        '-n', type='int', dest='num_bounces', default=1000000,
        help='The number of times to switch between the greenstacks')
    options, args = p.parse_args()

    if len(args) != 0:
        p.error('unexpected arguments: %s' % ', '.join(args))

    start_time = time.clock()
    switcher1 = greenstack.greenstack(switcher)
    switcher2 = greenstack.greenstack(switcher)
    switcher1.switch(options.num_bounces)
    print time.clock() - start_time, 'seconds'
