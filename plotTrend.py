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
from collections import OrderedDict
import itertools

log = logger.get(__name__)

# needed for camera-ready verion
import matplotlib
matplotlib.rcParams['ps.useafm'] = True
matplotlib.rcParams['pdf.use14corefonts'] = True
matplotlib.rcParams['text.usetex'] = True 

#matplotlib.rcParams['legend.handlelength'] = 0
matplotlib.rcParams['legend.numpoints'] = 1 # remove multiple markers in legend. Can also use plt.legend(numpoints=1)

def readOptions():
	parser = argparse.ArgumentParser()
	parser.add_argument('-x', "--xaxis", required=True, default="", help="X-axis")
	parser.add_argument('-y', "--yaxis", required=True, help="Y-axis")
	parser.add_argument('-e', "--error", required=False, default=False, help="Plot error", action='store_true')
	parser.add_argument('-f', "--ofile", required=False, help="Output file")
	parser.add_argument('-p', "--project", required=True, help="Folder with all run configurations")
	parser.add_argument('-i', "--ignore", required=False, help="List of project names to ignore", type=str)
	parser.add_argument('-o', "--only", required=False, help="List of projects to process", type=str)
	parser.add_argument('-l', "--legend-location", required=False, default="lower center", help="Location of the legend", type=str)
	args = parser.parse_args()
	
	if args.ignore:
		args.ignore = [item for item in args.ignore.split(',')]
	if args.only:
		args.only = [item for item in args.only.split(',')]
	return args

def flip(items, ncol):
	return itertools.chain(*[items[i::ncol] for i in range(ncol)])

# python plot_trend.py -p eve/ -y "Edge Coverage (AFL's plot\_data)" -x "Time (hrs)" -e
def main(options):
	
	xaxis = args.xaxis
	yaxis = args.yaxis
	ofile = args.ofile
	error = args.error
	project_folder = os.path.realpath( os.path.expanduser(args.project) )
	ignore_list = args.ignore if args.ignore != None else []
	only_list = args.only if args.only != None else []
	legend_location = args.legend_location
	
	if only_list and ignore_list:
		raise ValueError("-o and -i are mutually exclusive")

	# mapping between folder names and name for figure
	name_mappings={"qsym": "QSYM", "o-o-o-newdict": "V\\_AD", "o-n-c-all-olddict": "C\\_MD\\_FBSP", "o-o-o-oldict": "V\\_MD", \
					"o-n-c-all-noopt": "C\\_AD\\_FBSP", "o-n-c-all-opt": "C\\_OD\\_FBSP", "o-n-c-notdict-noopt": "C\\_AD\\_LBSP", \
					"o-n-c-none-opt": "C\\_OD", "o-n-c-all-none": "C\\_FBSP", "angora": "Angora", "o-n-c-nodict-opt": "C\\_OD\\_LBSP",
					"o-o-o-none": "V\\_O3", "o-o-o-none0": "V\\_O0", "o-o-o-mopt" : "MOpt", "o-o-l": "LAF-INTEL", "o-o-l-cc": "C\\_LAF-INTEL", \
						"o-n-c-none-newdict": "C\\_AD"}


	subfolders = [fn for fn in os.listdir(project_folder) if storage.isdir(os.path.join(project_folder, fn)) if fn != "in" and not fn.startswith("__results_")]
	log.info("Found %u projects", len(subfolders))

	# read project and put them in our list
	projects_data = []
	themin = sys.maxint
	for fn in subfolders:
	
		if (fn in ignore_list) or (only_list and fn not in only_list):
			log.info("Ignoring project %s", fn)
			continue
		log.info("Reading project %s", fn)

		data = storage.read_object(os.path.join(project_folder, fn, "trend.pik"))
		projects_data.append(data)
		if len(data["means"]) < themin:
			themin = len(data["means"])
		'''
		{"name", "times": , "means": , "stds": , "tops": , "bottoms": }
		'''
	
	log.info("\n")

	assert(projects_data)
	log.info("Plotting ...")

	times = [elt/float(3600) for elt in projects_data[0]["times"]]

	cm = plt.get_cmap('gist_rainbow')
	
	
	## plot stuff
	fig, ax = plt.subplots()
	ax.spines["top"].set_visible(False)
	ax.spines["right"].set_visible(False)
	ax.yaxis.tick_left()
	ax.xaxis.tick_bottom()
	ax.xaxis.set_ticks_position("none")
	ax.yaxis.grid()
	ax.xaxis.grid()
	ax.set_axisbelow(True)
	plt.ylabel(yaxis)
	plt.xlabel(xaxis)
	plt.xlim([0,times[-1]])
	#plt.ylim([0.8,1])
	#plt.title(title)

	# automatically pick spaced colors
	NUM_COLORS = len(projects_data)
	ax.set_color_cycle([cm(1.*i/NUM_COLORS) for i in range(NUM_COLORS)])

	# colors 'b', 'g', 'r', 'c', 'm', 'y', 'k', 'w'
	# markers : https://matplotlib.org/api/markers_api.html
	markers = ["o", "v", "^", "<", ">", "+", "x", "p", "*", "8", "d", "H", "D", "1", "2", "3", "."]
	assert(len(markers) >= NUM_COLORS)


	for m, p in zip(markers, projects_data):
		plot = plt.plot(times[:themin], p["means"][:themin], marker=m, label=name_mappings[p["name"]], markersize=6, linestyle="--")
		if error:
			# get_color() retrieves the one used by the previous plot
			plt.fill_between(times[:themin], p["tops"][:themin], p["bottoms"][:themin], facecolor=plot[-1].get_color(), linewidth=0, alpha=0.2)

	# legend. remove double marker
	if NUM_COLORS > 1:
		#leg = plt.legend(loc='lower right')
		handles, labels = ax.get_legend_handles_labels()
		leg =plt.legend(flip(handles, 2), flip(labels, 2), loc=legend_location, ncol=2)
		leg.get_frame().set_linewidth(0.0)
	# legend: use multiple columns
	# handles, labels = ax.get_legend_handles_labels()
	# handles = np.concatenate((handles[::2],handles[1::2]),axis=0)
	# labels = np.concatenate((labels[::2],labels[1::2]),axis=0)
	
	
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