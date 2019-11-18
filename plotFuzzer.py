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
	parser.add_argument('-y', "--yaxis", required=True, help="Y-axis")
	parser.add_argument('-l', "--limit", required=False, help="Y-axis limit as [start,end]", type=str)
	parser.add_argument(      "--no-percent", action="store_true", help="Use number and not percentage")
	parser.add_argument('-b', "--baseline", required=True, help="Baseline project name")
	parser.add_argument('-p', "--project", required=False, default=None, help="Project -- only needed if no --data is provided")
	parser.add_argument('-m', "--cummulative", required=False, action="store_true", help="Cummulative (default is comparison for each run)")
	# Note: those names are important, because they are hardcoded in stats_cov.py
	parser.add_argument('-c', "--coverage", choices=['Lines', 'Edges', 'BBs'])
	parser.add_argument('-d', "--data", required=False, default=None, help="List of tuples(name,(m1,std1),(m2,std2)). Only needed if not --project is provided", type=str)
	parser.add_argument('-o', "--ofile", required=False, help="Output file")
	parser.add_argument('-r', "--rotate_names", default="0", required=False, help="Rotate bar names by given value")
	args = parser.parse_args()

	args.data = list(eval(args.data)) if args.data else None
	args.limit = list(eval(args.limit)) if args.limit else None
	
	return args


# python plotFuzzer.py -r 25 -y "Functional lines (\%)" -d "[('F1',(0.1,0.02),(0.01,0.05)),('F2',(0.2,0.02),(0.02,0.05)),('F3',(0.4,0.02),(0.1,0.05)),('F4',(0.3,0.02),(0.02,0.05)),('F5',(0.45,0.02),(0.02,0.05)),('F6',(0.45,0.02),(0.02,0.05)), ('F7',(0.45,0.02),(0.02,0.05)),('F8',(0.45,0.02),(0.02,0.05)), ('F9','na', 'na'), ('F10',(0.45,0.02),(0.02,0.05)), ('F11',(0.45,0.02),(0.02,0.05))]"
# python plotFuzzer.py -r 75 -p . -y "Functional edges (\%)" -c "Edges" -o pdftohtml_FunctionEdges.pdf
# https://stackoverflow.com/questions/28931224/adding-value-labels-on-a-matplotlib-bar-chart
# this automatically adds the values above the bins
def add_value_labels(ax, spacing=5):
	"""Add labels to the end of each bar in a bar chart.

	Arguments:
	ax (matplotlib.axes.Axes): The matplotlib object containing the axes
	of the plot to annotate.
	spacing (int): The distance between the labels and the bars.
	"""

	# For each bar: Place a label
	for rect in ax.patches:
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
		label = "{:.1f}".format(y_value)

		# Create annotation
		ax.annotate(
			label,						# Use `label` as label
			(x_value, y_value),			# Place label at end of the bar
			xytext=(0, space),			# Vertically shift label by `space`
			textcoords="offset points",	# Interpret `xytext` as offset in points
			ha='center',				# Horizontally center label
			va=va)						# Vertically align label differently for


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
	

	title = args.title
	yaxis = args.yaxis
	data = args.data 
	rotate = args.rotate_names
	ofile = args.ofile
	limit = args.limit
	coverage = args.coverage
	is_cummulative = args.cummulative
	no_percent = args.no_percent
	baseline = args.baseline
	project_folder = os.path.realpath( os.path.expanduser(args.project) )

	if not data and not project_folder:
		raise ValueError("--data or --project are both missing")

	if data:
		log.info("Using data passes by argument")

		histo_names = [e[0] for e in data]
		# positive values
		phisto_values = [0 if type(e[1])==str else e[1][0] for e in data]
		phisto_std = [0 if type(e[1])==str else e[1][1] for e in data]
		pnan_bins = [True if type(e[1])==str else False for e in data]

		# negative values
		nhisto_values = [0 if type(e[2])==str else e[2][0] if e[2][0]<0 else -e[2][0] for e in data]
		nhisto_std = [0 if type(e[2])==str else e[2][1] for e in data]
		nnan_bins = [True if type(e[2])==str else False for e in data]

	else:
		# read the file and populate the data
		functional_coverage_pik = os.path.join(project_folder, "__results_%s" % baseline, "functional_coverage.pik")
		if not storage.file_exists(functional_coverage_pik):
			raise ValueError("'" + functional_coverage_pik +"' not found")

		log.info("No data passed as argument. Using functional_coverage.pik saved in project folder")
		functionalCov = storage.read_object(functional_coverage_pik)


		# mapping between folder names and name for figure
		name_mappings={"qsym": "QSYM", "o-o-o-newdict": "V\\_AD", "o-n-c-all-olddict": "C\\_MD\\_FBSP", "o-o-o-oldict": "V\\_MD", \
						"o-n-c-all-noopt": "C\\_AD\\_FBSP", "o-n-c-all-opt": "C\\_OD\\_FBSP", "o-n-c-notdict-noopt": "C\\_AD\\_LBSP", \
						"o-n-c-none-opt": "C\\_OD", "o-n-c-all-none": "C\\_FBSP", "angora": "Angora", "o-n-c-nodict-opt": "C\\_OD\\_LBSP",
						"o-o-o-none": "V\\_O3", "o-o-o-none0": "V\\_O0", "o-o-o-mopt" : "MOpt", "o-o-l": "LAF-INTEL", "o-o-l-cc": "C\\_LAF-INTEL", \
						"o-n-c-none-newdict": "C\\_AD"}

		histo_names = []
		# positive values
		phisto_values = []
		phisto_std = []
		pnan_bins = []

		# negative values
		nhisto_values = []
		nhisto_std = []
		nnan_bins = []

		# let's force an order for plotting
		ordered_list = ["o-o-o-none", "o-o-o-none0", "o-o-l", "o-o-o-mopt", "qsym", "angora", "o-o-o-oldict", "o-o-o-newdict", \
						"o-o-l-cc", "o-n-c-none-newdict", "o-n-c-all-none", "o-n-c-notdict-noopt", "o-n-c-all-olddict", "o-n-c-all-noopt", \
						"o-n-c-none-opt", "o-n-c-nodict-opt", "o-n-c-all-opt"]
		
		assert (baseline in ordered_list)

		for name in ordered_list:

			assert (name in name_mappings)

			if name in functionalCov:
				results = functionalCov[name][coverage] # use the result indicated by i command line

				histo_names.append(name_mappings[name])

				phisto_values.append(max(results[0][0], 0.001)) # force plotting the histo
				if is_cummulative:
					assert (results[0][1] == None) # should be None for cummulative
				phisto_std.append(results[0][1])
				pnan_bins.append(False)

				nhisto_values.append(results[1][0] if results[1][0]<0 else -results[1][0])
				nhisto_std.append(results[1][1])
				nnan_bins.append(False)

			elif not name == baseline:
			
				histo_names.append(name_mappings[name])

				phisto_values.append(1e-10) # small but not 0 to display properly at end/start
				phisto_std.append(0 if not is_cummulative else None)
				pnan_bins.append(True)

				nhisto_values.append(1e-10) # small but not 0 to display properly at end/start
				nhisto_std.append(0)
				nnan_bins.append(True)
	

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
	plt.axhline(y=0, color='black', linewidth=0.5)

	phigh = max(phisto_values)
	nlow = max(nhisto_values)

	if limit:
		assert(phigh <= limit[1] and nlow >= limit[0])
		ax.set_ylim(limit[0], limit[1])
	
	if is_cummulative:
		assert (phisto_std == [None]*len(phisto_std)) # make sure these are None for cummulative
		pbars = ax.bar(y_pos, [v if no_percent else 100*v for v in phisto_values], color='#7fc97f', align='center', alpha=0.75, edgecolor = "none", linewidth='0.2', error_kw=dict(lw=0.5, capsize=0.5, capthick=0.5))
		nbars = ax.bar(y_pos, [v if no_percent else 100*v for v in nhisto_values], color='#beaed4', align='center', alpha=0.75, edgecolor = "none", linewidth='0.2', error_kw=dict(lw=0.5, capsize=0.5, capthick=0.5))
	else:
		assert (not no_percent)
		#assert (phisto_std == [1]*len(phisto_std))
		pbars = ax.bar(y_pos, [100*v for v in phisto_values], yerr=[100*v for v in phisto_std], color='#7fc97f', align='center', alpha=0.75, edgecolor = "none", linewidth='0.2', error_kw=dict(lw=0.5, capsize=0.5, capthick=0.5))
		nbars = ax.bar(y_pos, [100*v for v in nhisto_values], yerr=[100*v for v in nhisto_std], color='#beaed4', align='center', alpha=0.75, edgecolor = "none", linewidth='0.2', error_kw=dict(lw=0.5, capsize=0.5, capthick=0.5))
	plt.ylabel(yaxis)
	plt.title(title)

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