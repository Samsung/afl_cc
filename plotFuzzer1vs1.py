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
	parser.add_argument('-b', "--baseline", required=True, help="Baseline")
	parser.add_argument('-p', "--project", required=True, help="project folder")
	parser.add_argument('-f', "--fuzzer", required=True, help="fuzzers to compare")
	parser.add_argument('-c', "--coverage", choices=['Lines', 'Edges', 'BBs'])
	parser.add_argument('-o', "--ofile", required=False, help="Output file")
	parser.add_argument('-n', "--na", required=False, help="List of project names to treat as NA in plot", type=str)
	parser.add_argument("--ignore-projects", required=False, help="List of project names to ignore", type=str)
	parser.add_argument('--only-projects', required=False, help="List of projects to process", type=str)
	parser.add_argument('-r', "--rotate_names", default="0", required=False, help="Rotate bar names by given value")
	parser.add_argument('-y', "--yaxis", required=True, help="Y-axis")

	args = parser.parse_args()

	if args.na:
		args.na = [item for item in args.na.split(',')]
	if args.ignore_projects:
		args.ignore_projects = [item for item in args.ignore_projects.split(',')]
	if args.only_projects:
		args.only_projects = [item for item in args.only_projects.split(',')]

	return args

def add_na_labels(ax, nan_bins, spacing=5):
	"""Add labels to the end of each bar in a bar chart.

	Arguments:
	ax (matplotlib.axes.Axes): The matplotlib object containing the axes
	of the plot to annotate.
	spacing (int): The distance between the labels and the bars.
	"""

	# For each bar: Place a label
	for rect,do_nan in zip(ax.patches, nan_bins):

		if not do_nan:
			continue

		# Get X and Y placement of label from rect.
		y_value = rect.get_height()
		x_value = rect.get_x() + rect.get_width() / 2

		# Number of points between bar and label. Change to your liking.
		space = spacing
		# Vertical alignment for positive values
		va = 'bottom'

		# If value of bar is negative: Place label below bar
		if y_value < 0:
			# Invert space to place label below
			space *= -1
			# Vertically align label at top
			va = 'top'

		# Use Y value as label and format number with one decimal place
		label = "N/A"

		# Create annotation
		ax.annotate(
			label,						# Use `label` as label
			(x_value, y_value),			# Place label at end of the bar
			xytext=(0, space),			# Vertically shift label by `space`
			textcoords="offset points",	# Interpret `xytext` as offset in points
			ha='center',				# Horizontally center label
			va=va)						# Vertically align label differently for



def main(options):
	
	# get the params
	fuzzer = args.fuzzer
	baseline = args.baseline
	project_folder = os.path.realpath(os.path.expanduser(args.project))
	ofile = args.ofile
	coverage = args.coverage
	only_projects = args.only_projects
	ignore_projects = args.ignore_projects
	rotate = args.rotate_names
	yaxis = args.yaxis
	na = args.na

	projects = {"alice": ["jpeg", "pdftohtml"], \
				"amir": ["gumbo-html"], \
				"darius": ["xml"], \
				"eve": ["jhead", "png"], \
				"marc": ["ar", "readelf"], \
				"pubali": ["objdump"], \
				"shuyingTwo": ["unrtf"]
				}

	# mapping between directory names and image names
	projects_mapping = {"jpeg": "jpeg", "pdftohtml":"pdftohtml", "gumbo-html":"prettyprint", \
						"xml": "xmllint", "jhead": "jhead", "png":"libpng\\_read\\_fuzzer", \
						"ar":"ar", "readelf":"readelf", "objdump":"objdump", "unrtf":"unrtf"}

	# mapping between folder names and name for figure
	fuzzers = ["qsym", "o-o-o-newdict", "o-n-c-all-olddict", "o-o-o-oldict", \
					"o-n-c-all-noopt", "o-n-c-all-opt", "o-n-c-notdict-noopt", \
					"o-n-c-none-opt" "o-n-c-all-none", "angora", "o-n-c-nodict-opt",
					"o-o-o-none", "o-o-o-none0", "o-o-o-mopt", "o-o-l", "o-o-l-cc", \
					"o-n-c-none-newdict"]

	assert (baseline in fuzzers)

	histo_names = []
	# positive values
	phisto_values = []
	phisto_std = []
	pnan_bins = []

	# negative values
	nhisto_values = []
	nhisto_std = []
	nnan_bins = []

	for machine in projects.keys():
		for project in projects[machine]:

			in_na = na and project in na

			if ignore_projects and project in ignore_projects and not in_na:
				log.info("Ignoring project %s", project)
				continue

			if only_projects and project not in only_projects and not in_na:
				log.info("Ignoring project %s", project)
				continue

			if not in_na:
				project_location = os.path.join(project_folder, machine, project)
				log.info("Processing project %s (%s)", project, project_location)

				functional_coverage_pik = os.path.join(project_location, "__results_%s" % baseline, "functional_coverage.pik")
				if not storage.file_exists(functional_coverage_pik):
					raise ValueError("'" + functional_coverage_pik +"' not found")

				functionalCov = storage.read_object(functional_coverage_pik)

				# name is a fuzzing config
				for name in functionalCov.keys():
					
					if name != fuzzer:
						continue

					log.info("Processing fuzzer %s", name)

					results = functionalCov[name][coverage] # use the result indicated by i command line

					assert (project in projects_mapping)
					histo_names.append(projects_mapping[project])

					phisto_values.append(max(results[0][0], 0.001)) # force plotting the histo
					# support only cummulative
					assert (results[0][1] == None) # should be None for cummulative
					phisto_std.append(results[0][1])
					pnan_bins.append(False)

					nhisto_values.append(results[1][0] if results[1][0]<0 else -results[1][0])
					nhisto_std.append(results[1][1])
					nnan_bins.append(False)
			else:

				histo_names.append(projects_mapping[project])

				phisto_values.append(1e-10) # small but not 0 to display properly at end/start
				phisto_std.append(None)
				pnan_bins.append(True)

				nhisto_values.append(1e-10) # small but not 0 to display properly at end/start
				nhisto_std.append(0)
				nnan_bins.append(True)

			log.info("")

	y_pos = np.arange(len(histo_names))

	assert(nnan_bins == pnan_bins)
	nan_bins = pnan_bins

	fig, ax = plt.subplots()
	ax.spines["top"].set_visible(False)
	ax.spines["right"].set_visible(False)
	ax.yaxis.tick_left()
	ax.xaxis.tick_bottom()
	ax.xaxis.set_ticks_position("none")
	ax.yaxis.grid(linestyle=":") # dotted grid
	ax.set_axisbelow(True) 
	plt.xticks(y_pos, histo_names)
	plt.xticks(xrange(len(histo_names)), rotation=rotate)

	phigh = max(phisto_values)
	nlow = max(nhisto_values)

	pbars = ax.bar(y_pos, [v*100 for v in phisto_values], color='#7fc97f', align='center', alpha=0.75, edgecolor = "none", linewidth='0.2', error_kw=dict(lw=0.5, capsize=0.5, capthick=0.5))
	nbars = ax.bar(y_pos, [v*100 for v in nhisto_values], color='#beaed4', align='center', alpha=0.75, edgecolor = "none", linewidth='0.2', error_kw=dict(lw=0.5, capsize=0.5, capthick=0.5))

	plt.ylabel(yaxis)
	#plt.title(title)

	add_na_labels(ax, nan_bins)
	 
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