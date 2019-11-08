import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)

import storage

def read(fn):
	d = dict()
	lines = storage.read_file(fn, True)
	for line in lines:
		edgeid = int(line.split("=")[0])
		d[edgeid] = line[:-1].split("=")[1].split(",")
	return d