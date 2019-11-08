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
	parser.add_argument('-b', "--baseline_folder", required=False, default="o-o-o-none", help="where to find the stability.pik")
	parser.add_argument('-c', "--coverage", required=True, choices=['Lines', 'Edges', 'BBs'])
	parser.add_argument('-s', "--stats", choices=['mean', 'median'], required=True, help="median/95-percentile oe mean/std")
	parser.add_argument("--ignore-projects", required=False, help="List of project names to ignore", type=str)
	parser.add_argument('--only-projects', required=False, help="List of projects to process", type=str)
	parser.add_argument("--ignore-fuzzers", required=False, help="List of fuzzers to ignore", type=str)
	parser.add_argument('--only-fuzzers', required=False, help="List of fuzzer to process", type=str)

	args = parser.parse_args()

	if args.ignore_projects:
		args.ignore_projects = [item for item in args.ignore_projects.split(',')]
	if args.only_projects:
		args.only_projects = [item for item in args.only_projects.split(',')]
	if args.ignore_fuzzers:
		args.ignore_fuzzers = [item for item in args.ignore_fuzzers.split(',')]
	if args.only_fuzzers:
		args.only_fuzzers = [item for item in args.only_fuzzers.split(',')]

	return args


def main(options):
	
	# get the params
	project_folder = os.path.realpath(os.path.expanduser(args.project))
	coverage = args.coverage
	only_projects = args.only_projects
	ignore_projects = args.ignore_projects
	only_fuzzers = args.only_fuzzers
	ignore_fuzzers = args.ignore_fuzzers
	baseline = args.baseline_folder
	stats_style = args.stats
	
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

	fuzzers_mappings={"qsym": "QSYM", "o-o-o-newdict": "V\\_AD", "o-n-c-all-olddict": "C\\_MD\\_FBSP", "o-o-o-oldict": "V\\_MD", \
						"o-n-c-all-noopt": "C\\_AD\\_FBSP", "o-n-c-all-opt": "C\\_OD\\_FBSP", "o-n-c-notdict-noopt": "C\\_AD\\_LBSP", \
						"o-n-c-none-opt": "C\\_OD", "o-n-c-all-none": "C\\_FBSP", "angora": "Angora", "o-n-c-nodict-opt": "C\\_OD\\_LBSP",
						"o-o-o-none": "V\\_O3", "o-o-o-none0": "V\\_O0", "o-o-o-mopt" : "MOpt", "o-o-l": "LAF\\_INTEL", "o-o-l-cc": "C\\_LAF-INTEL", \
						"o-n-c-none-newdict": "C\\_AD"}


	fuzzers_ordered = ["o-o-o-none", "o-o-o-none0", "o-o-l", "o-o-o-mopt", "qsym", "angora", "o-o-o-oldict", "o-o-o-newdict", \
						"o-o-l-cc", "o-n-c-none-newdict", "o-n-c-all-none", "o-n-c-notdict-noopt", "o-n-c-all-olddict", "o-n-c-all-noopt", \
						"o-n-c-none-opt", "o-n-c-nodict-opt", "o-n-c-all-opt"]

	
	stats_results = {}
	for machine in projects.keys():
		for project in projects[machine]:

			if ignore_projects and project in ignore_projects:
				#log.info("Ignoring project %s\n", project)
				continue

			if only_projects and project not in only_projects:
				#log.info("Ignoring project %s\n", project)
				continue

			stats_results[project] = {}

			project_location = os.path.join(project_folder, machine, project)
			log.info("Processing %s (%s)", project, project_location)

			stability_pik = os.path.join(project_location, "__results_%s" % baseline, "stability2.pik")
			if not storage.file_exists(stability_pik):
				raise ValueError("'" + stability_pik +"' not found")

			log.info("Using stability data from %s", stability_pik)

			project_stability = storage.read_object(stability_pik)

			# name is a fuzzing config
			for name in project_stability.keys():
				
				if (ignore_fuzzers and name in ignore_fuzzers):
					#log.info("Ignoring fuzzer %s\n", name)
					continue
				
				if (only_fuzzers and name not in only_fuzzers):
					#log.info("Ignoring fuzzer %s\n", name)
					continue
				
				log.info("Processing %s", name)

				results = project_stability[name][coverage]

				
				scores = [results[line] for line in results.keys()]
				if stats_style == "mean":
					score_m, _, score_low, score_up = statistics.mean_std_confidence_interval(scores)
					mad = None
					# this makes no sense because the distribution is NOT normal...
				else:
					score_m = statistics.median(scores)
					mad = statistics.median_absolute_deviation(scores)
					score_up = score_m + mad/2.
					score_low = score_m - mad/2.
				#print name, score_median, score_low_percentile, score_up_percentile

				stats_results[project][name] = {"m": score_m, "up": score_up, "low": score_low, "mad": mad}
				

			log.info("")

	# title
	title_line = ""
	for fuzzer in fuzzers_ordered:
		if ignore_fuzzers and fuzzer in ignore_fuzzers:
			continue

		if only_fuzzers and fuzzer not in only_fuzzers:
			continue

		title_line += " && %s" % fuzzers_mappings[fuzzer]

	print title_line

	for project in projects_mapping.keys():
		if ignore_projects and project in ignore_projects:
			continue

		if only_projects and project not in only_projects:
			continue

		line = projects_mapping[project]
		for fuzzer in fuzzers_ordered:
			if fuzzer not in stats_results[project]:
				line += " && %s" % ("\\NA")	
			else:
				if mad is None:
					line += " && %1.f / %.1f - %.1f" % (stats_results[project][fuzzer]["m"], stats_results[project][fuzzer]["low"], stats_results[project][fuzzer]["up"])
				else:
					line += " && %1.f / %.1f" % (stats_results[project][fuzzer]["m"], stats_results[project][fuzzer]["mad"])
		print line

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