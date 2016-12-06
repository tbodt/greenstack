#!/usr/bin/env python
import greenstack
from time import clock

main = greenstack.getcurrent()
num_links = 100000

def link(value):
    if value == num_links:
        print 'Result:'
        return;
    g = greenstack.greenlet(link)
    g.parent = main
    g.switch(value + 1)

start_time = clock()
link(0)
print clock() - start_time, 'seconds'

