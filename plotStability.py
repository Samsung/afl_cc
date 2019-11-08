import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)
from python_libs import *
import numpy as np
import matplotlib.mlab as mlab
import matplotlib.pyplot as plt
import math
import argparse, traceback, sys, errno
import ast

log = logger.get(__name__)

# needed for camera-ready verion
import matplotlib
matplotlib.rcParams['ps.useafm'] = True
matplotlib.rcParams['pdf.use14corefonts'] = True
matplotlib.rcParams['text.usetex'] = True 

def readOptions():
	parser = argparse.ArgumentParser()
	parser.add_argument('-t', "--title", required=False, default="", help="Title")
	parser.add_argument('-d', "--data", required=False, default=None, help="List of 10 values", type=str)
	parser.add_argument('-l', "--limit", required=False, help="Y-axis limit", type=int)
	parser.add_argument('-p', "--project", required=False, default=None, help="Project -- only needed if no --data is provided")
	parser.add_argument('-f', "--fuzzer", required=False, default=None, help="Fuzzer folder")
	parser.add_argument('-n', "--nruns", type=int, required=True, help="Minimum number of runs")
	# Note: those names are important, because they are hardcoded in stats_cov.py
	parser.add_argument('-c', "--coverage", choices=['Lines', 'Edges', 'BBs'])
	parser.add_argument('-o', "--ofile", required=False, help="Output file")
	parser.add_argument('-a', "--allbin", default=False, dest='allbin', action='store_true', help="Print all bins including those with 0 height")
	args = parser.parse_args()

	args.data = list(eval(args.data)) if args.data else None

	return args


# python plotStability.py -a -d [35,40,15,5,3,1,1,0,0,0]

def main(options):
	
	nruns = args.nruns
	data = args.data
	limit = args.limit
	ofile = args.ofile
	allbin = args.allbin
	fuzzer = args.fuzzer.replace("/", "") # just in case the user passes the folder...
	coverage = args.coverage
	project_folder = os.path.realpath( os.path.expanduser(args.project) )

	title = args.title
	histo_names = [str(i)+"\\%" for i in xrange(10,110,10)][::-1]

	if data:
		if project_folder or fuzzer or coverage:
			raise ValueError("--data imcompatible with --project, --fuzzer and --coverage")

		log.info("Using data provided thru --data")

	if not data:
		
		if not (project_folder and fuzzer and coverage):
			raise ValueError("--project and --fuzzer --coverage are required")

		stability_pik = os.path.join(project_folder, "stability.pik")
		log.info("Using data from '%s'", stability_pik)

		if not storage.file_exists(stability_pik):
			raise ValueError("'" + stability_pik +"' not found")

		project_stability = storage.read_object(stability_pik)
		tmp_data = project_stability[fuzzer][coverage]
		the_sum = sum([tmp_data[elt] for elt in tmp_data])
		len_data = len(tmp_data)
		normalized_data = [0]*len_data
		for k, r in tmp_data.iteritems():
			normalized_data[len_data-k] = r/float(the_sum)*100.
		data = normalized_data
		assert(len(data) >= nruns)
		# Note: project_stability["all" + coverage] = all covered
	

	histo_values = data
	xaxis = histo_names
	yaxis = coverage + " covered (\\%)"
	y_pos = np.arange(len(histo_names))

	the_sum = sum(histo_values)
	assert(np.isclose(the_sum, 100, rtol=1e-05, atol=1e-08, equal_nan=False))

	fig, ax = plt.subplots()
	ax.spines["top"].set_visible(False)
	ax.spines["right"].set_visible(False)
	ax.yaxis.tick_left()
	ax.xaxis.tick_bottom()
	ax.xaxis.set_ticks_position("none")
	ax.yaxis.grid(linestyle=":") # dotted grid
	ax.set_axisbelow(True) 
	plt.xticks(y_pos, histo_names)

	phigh = max(histo_values)

	if limit:
		assert(phigh <= limit)
		plt.ylim(0, limit)
	else:
		plt.ylim([0,math.ceil(max(histo_values))])
	epsilon = 1e-7 if allbin else 0 # force zero bins to be plotted

	pbars = ax.bar(y_pos, [v+epsilon for v in histo_values], align='center', alpha=0.4, edgecolor = "none")
	plt.ylabel(yaxis)
	plt.title(title)
	
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