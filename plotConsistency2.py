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
	parser.add_argument('-p', "--project", required=False, default=None, help="Project -- only needed if no --data is provided")
	parser.add_argument('-f', "--fuzzers", required=False, type=str, default=None, help="Fuzzer folder")
	parser.add_argument('-n', "--nruns", type=int, required=True, help="Minimum number of runs")
	# Note: those names are important, because they are hardcoded in stats_cov.py
	parser.add_argument('-c', "--coverage", choices=['Lines', 'Edges', 'BBs'])
	parser.add_argument('-o', "--ofile", required=False, help="Output file")
	parser.add_argument('-a', "--allbin", default=False, dest='allbin', action='store_true', help="Print all bins including those with 0 height")
	args = parser.parse_args()

	args.fuzzers = [e for e in args.fuzzers.replace(" ","").split(",")]

	return args


# python plotStability.py -a -d [35,40,15,5,3,1,1,0,0,0]

def main(options):
	
	nruns = args.nruns
	ofile = args.ofile
	allbin = args.allbin
	fuzzers = [e.replace("/","") for e in args.fuzzers] # just in case the user passes the folder...
	coverage = args.coverage
	project_folder = os.path.realpath( os.path.expanduser(args.project) )
	baseline_fuzzer = fuzzers[0]
	title = args.title
	
	if not (project_folder and fuzzers and coverage):
		raise ValueError("--project and --fuzzers --coverage are required")

	datas = dict()
	union_lines_visited = dict()

	name_mappings={"qsym": "QSYM", "o-o-o-newdict": "V\\_AD", "o-n-c-all-olddict": "C\\_MD\\_FBSP", "o-o-o-oldict": "V\\_MD", \
						"o-n-c-all-noopt": "C\\_AD\\_FBSP", "o-n-c-all-opt": "C\\_OD\\_FBSP", "o-n-c-notdict-noopt": "C\\_AD\\_LBSP", \
						"o-n-c-none-opt": "C\\_OD", "o-n-c-all-none": "C\\_FBSP", "angora": "Angora", "o-n-c-nodict-opt": "C\\_OD\\_LBSP", \
						"o-o-o-none": "V\\_O3", "o-o-o-none0": "V\\_O0", "o-o-o-mopt" : "MOpt", "o-o-l": "LAF-INTEL", \
						"o-o-l-cc": "C\\_LAF-INTEL", "o-n-c-none-newdict": "C\\_AD"}
	markers = ["o", "v", "^", "<", ">", "+", "x", "p", "*", "8", "d", "H", "D", "1", "2", "3"]

	assert (len(markers) >= len(fuzzers))

	# first get the union of all visited lines
	for fuzzer in fuzzers:
		stability_pik = os.path.join(project_folder, "stability2.pik")
		log.info("Using data from '%s' for fuzzer %s", stability_pik, fuzzer)

		if not storage.file_exists(stability_pik):
			raise ValueError("'" + stability_pik +"' not found")

		project_stability = storage.read_object(stability_pik)
		tmp_dict = project_stability[fuzzer][coverage]
		for key in tmp_dict.keys():
			union_lines_visited[key] = True

	ordered_lines_occurence = sorted(project_stability[baseline_fuzzer][coverage].items(),key=lambda item: item[1], reverse=True)
	ordered_lines_visited = [e[0] for e in ordered_lines_occurence]
	for line in union_lines_visited:
		if line not in ordered_lines_visited:
			ordered_lines_visited.append(line)
	#print ordered_lines_visited
	#exit(0)
	
	# now get the lines for each fuzzer
	for fuzzer in fuzzers:

		project_stability = storage.read_object(stability_pik)
		assert(fuzzer not in datas)
		datas[fuzzer] = []
		previous_occurence = 100
		tmp_data = project_stability[fuzzer][coverage]
		for line in ordered_lines_visited:
			occurence = tmp_data[line] if line in tmp_data else 0
			#print line, occurence, previous_occurence
			assert (occurence <= nruns)
			# if occurence > previous_occurence and fuzzer == baseline_fuzzer:
			# 	print line, occurence, project_stability[baseline_fuzzer][coverage][line]
			assert (occurence <= previous_occurence or fuzzer != baseline_fuzzer) # should come decrementally
			previous_occurence = occurence 
			datas[fuzzer].append(int(occurence*100./nruns))
			
		# Note: project_stability["all" + coverage] = all covered
	
	# histo_values = data
	# xaxis = histo_names
	yaxis = coverage + " covered (\\%)"
	y_pos = np.arange(len(datas))

	# the_sum = sum(histo_values)
	# assert(np.isclose(the_sum, 100, rtol=1e-05, atol=1e-08, equal_nan=False))


	fig, ax = plt.subplots()
	ax.spines["top"].set_visible(False)
	ax.spines["right"].set_visible(False)
	ax.yaxis.tick_left()
	ax.xaxis.tick_bottom()
	ax.xaxis.set_ticks_position("none")
	ax.yaxis.grid(linestyle=":") # dotted grid
	ax.set_axisbelow(True) 
	#plt.xticks(y_pos, histo_names)
	m = 1
	print baseline_fuzzer
	plt.plot(datas[baseline_fuzzer], marker=markers[0], linestyle="", markersize=4)
	for fuzzer in fuzzers[1:]:
		print fuzzer
		plt.plot([e-100 for e in datas[fuzzer]], marker=markers[m], linestyle="", markersize=4)
		m += 1

	#plt.axis([0, 6, 0, 20])
	
	# plt.ylim([0,math.ceil(max(histo_values))])
	# epsilon = 1e-7 if allbin else 0 # force zero bins to be plotted

	# pbars = ax.bar(y_pos, [v+epsilon for v in histo_values], align='center', alpha=0.4, edgecolor = "none")
	# plt.ylabel(yaxis)
	# plt.title(title)
	
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