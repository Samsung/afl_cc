import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)
from python_libs import *
import numpy as np
import matplotlib.mlab as mlab
import matplotlib.pyplot as plt
import math as mt
import argparse, traceback, sys, errno

log = logger.get(__name__)

# needed for camera-ready verion
import matplotlib
matplotlib.rcParams['ps.useafm'] = True
matplotlib.rcParams['pdf.use14corefonts'] = True
matplotlib.rcParams['text.usetex'] = True 

def readOptions():
	parser = argparse.ArgumentParser()
	parser.add_argument('-p', "--project", required=True, help="project folder")
	parser.add_argument('-f', "--fuzzers", required=True, help="fuzzers to compare", type=str)
	parser.add_argument('-n', "--nbugs", required=True, help="number of bugs", type=int)
	parser.add_argument('-o', "--ofile", required=False, help="Output file")
	parser.add_argument('-r', "--rotate_names", default="0", required=False, help="Rotate bar names by given value")
	parser.add_argument('-y', "--yaxis", required=True, help="Y-axis")
	parser.add_argument('-l', "--limit", required=False, help="Y-axis limit", type=int)

	args = parser.parse_args()

	args.fuzzers = [item for item in args.fuzzers.split(',')]
	
	return args


def main(options):
	
	# get the params
	fuzzers = args.fuzzers
	project_folder = os.path.realpath(os.path.expanduser(args.project))
	ofile = args.ofile
	rotate = args.rotate_names
	nbugs = args.nbugs
	yaxis = args.yaxis
	limit = args.limit

	limit = min(limit, 100) if limit else 100
	# fuzzers[0] is first fuzzer
	# fuzzers[1] is second fuzzer

	# example of having consistency between 0 and 100 with step 10 (since we have just 10 runs in our case)
	import random
	fuzzer1_bugs = [random.randrange(0, 101, step=10) for e in xrange(nbugs)]
	fuzzer2_bugs = [random.randrange(0, 101, step=10) for e in xrange(nbugs)]

	# force 0 to be visible
	fuzzer1_bugs = [e if e != 0 else 0.001 for e in fuzzer1_bugs]
	fuzzer2_bugs = [e if e != 0 else 0.001 for e in fuzzer2_bugs]

	assert (len(fuzzer1_bugs) == len(fuzzer2_bugs))
	y_pos1 = np.arange(len(fuzzer1_bugs))
	y_pos2 = np.arange(len(fuzzer1_bugs)) + 0.4
	y_ticks = (y_pos1+y_pos2)/2.
	y_labels = ["\\#%s" % str(i) for i in xrange(1,len(y_ticks)+1)]
	
	fig, ax = plt.subplots()
	ax.spines["top"].set_visible(False)
	ax.spines["right"].set_visible(False)
	ax.yaxis.tick_left()
	ax.xaxis.tick_bottom()
	ax.xaxis.set_ticks_position("none")
	ax.yaxis.grid(linestyle=":") # dotted grid
	ax.set_axisbelow(True)
	ax.set_ylim(0, limit)
	plt.xticks(y_ticks, y_labels)
	#plt.xticks(xrange(len(histo_names)), rotation=rotate)

	bars1 = ax.bar(y_pos1, fuzzer1_bugs, width=0.4, color='#7fc97f', align='center', alpha=0.75, edgecolor = "none", linewidth='0.2', error_kw=dict(lw=0.5, capsize=0.5, capthick=0.5))
	bars2 = ax.bar(y_pos2, fuzzer2_bugs, width=0.4, color='#beaed4', align='center', alpha=0.75, edgecolor = "none", linewidth='0.2', error_kw=dict(lw=0.5, capsize=0.5, capthick=0.5))

	plt.ylabel(yaxis)
	#plt.title(title)
	 
	plt.tight_layout()
	if ofile:
		plt.savefig(ofile, transparent = True, bbox_inches='tight')
	else:
		plt.show()

	plt.close()

if __name__ == '__main__':
		
	args = readOptions()

	try:
		main(args)
		
	except:
		log.exception("exception caught")
		#traceback.print_exc(file=sys.stdout)
	#finally:
		#print 'finally'

	log.info('Done ...')