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
	parser.add_argument('-b', "--baseline-folder", required=False, default="o-o-o-none", help="where to find the stability.pik")
	parser.add_argument('-f', "--fuzzer", required=True, help="fuzzer for which we want the unique coverage covered")
	parser.add_argument('-d', "--diff", required=False, type=int, help="difference for stability score")
	parser.add_argument('-c', "--coverage", required=True, choices=['Lines', 'Edges', 'BBs'])
	parser.add_argument('-v', "--verbose",  default=False, action='store_true')
	parser.add_argument('-t', "--type", required=True, choices=['Unique', 'Best'])
	parser.add_argument('-l', "--list-contenders", required=False, help="List of fuzzers to compare against", type=str)
	# parser.add_argument('--only-projects', required=False, help="List of projects to process", type=str)
	# parser.add_argument("--ignore-fuzzers", required=False, help="List of fuzzers to ignore", type=str)
	# parser.add_argument('--only-fuzzers', required=False, help="List of fuzzer to process", type=str)

	args = parser.parse_args()

	if args.list_contenders:
		args.list_contenders = [item for item in args.list_contenders.split(',')]
	# if args.only_projects:
	# 	args.only_projects = [item for item in args.only_projects.split(',')]
	# if args.ignore_fuzzers:
	# 	args.ignore_fuzzers = [item for item in args.ignore_fuzzers.split(',')]
	# if args.only_fuzzers:
	# 	args.only_fuzzers = [item for item in args.only_fuzzers.split(',')]

	return args


def main(options):
	
	# get the params
	project_folder = os.path.realpath(os.path.expanduser(args.project))
	coverage = args.coverage
	baseline = args.baseline_folder
	fuzzer = args.fuzzer
	verbose = args.verbose
	ttype = args.type
	diff = args.diff
	list_contenders = args.list_contenders
	
	stability_pik = os.path.join(project_folder, "__results_%s" % baseline, "stability2.pik")
	if not storage.file_exists(stability_pik):
		raise ValueError("'" + stability_pik +"' not found")

	log.info("Using stability data from %s", stability_pik)

	project_stability = storage.read_object(stability_pik)
	
	fuzzers_list = ["o-o-o-none", "o-o-o-none0", "o-o-l", "o-o-o-mopt", "qsym", "angora", "o-o-o-oldict", "o-o-o-newdict", \
						"o-o-l-cc", "o-n-c-none-newdict", "o-n-c-all-none", "o-n-c-notdict-noopt", "o-n-c-all-olddict", "o-n-c-all-noopt", \
						"o-n-c-none-opt", "o-n-c-nodict-opt", "o-n-c-all-opt", "o-n-c-none-newdict"]
	
	fuzzers_mappings={"qsym": "QSYM", "o-o-o-newdict": "V_AD", "o-n-c-all-olddict": "C_MD_FBSP", "o-o-o-oldict": "V_MD", \
						"o-n-c-all-noopt": "C_AD_FBSP", "o-n-c-all-opt": "C_OD_FBSP", "o-n-c-notdict-noopt": "C_AD_LBSP", \
						"o-n-c-none-opt": "C_OD", "o-n-c-all-none": "C_FBSP", "angora": "Angora", "o-n-c-nodict-opt": "C_OD_LBSP",
						"o-o-o-none": "V_O3", "o-o-o-none0": "V_O0", "o-o-o-mopt" : "MOpt", "o-o-l": "LAF_INTEL", "o-o-l-cc": "C_LAF-INTEL", \
						"o-n-c-none-newdict": "C_AD"}

	assert (fuzzer in fuzzers_list)

	if ttype == "Best":
		if diff is None:
			raise ValueError("Missing --diff option")

		log.info("using difference %u", diff)

		not_best_lines = set()
		fuzzer_results = {}
		for name in fuzzers_list:

			if name in project_stability:

				if list_contenders and name not in list_contenders and name != fuzzer:
					log.info("Ignoring %s (%s)", fuzzers_mappings[name], name)
					continue

				results = project_stability[name][coverage] # use the result indicated by i command line
				
				fuzzer_results[name] = results
		
		log.info("Calculating best %s in %s (%s) with difference of %u", coverage, fuzzers_mappings[fuzzer], fuzzer, diff)

		fuzzer_cov = fuzzer_results[fuzzer]

		for name in fuzzer_results.keys():
			if name == fuzzer:
				continue

			log.info("")
			log.info("Processing %s (%s)", fuzzers_mappings[name], name)

			curr_fuzzer = fuzzer_results[name]
			
			for line in curr_fuzzer.keys():
				if line not in fuzzer_cov.keys() or curr_fuzzer[line] > fuzzer_cov[line] - diff:
					not_best_lines.add(line)

		best_lines = set(fuzzer_cov) - not_best_lines
		log.info("Best %s: %u", coverage, len(best_lines))

		fuzzer_best_ordered = sorted(best_lines)
		if verbose:
			for line in fuzzer_best_ordered:
				print line, fuzzer_cov[line]
	else:

		fuzzer_results = {}
		for name in fuzzers_list:

			if name in project_stability:

				if list_contenders and name not in list_contenders and name != fuzzer:
					log.info("Ignoring %s (%s)", fuzzers_mappings[name], name)
					continue

				log.info("Processing %s (%s)", fuzzers_mappings[name], name)
				results = project_stability[name][coverage] # use the result indicated by i command line
				
				fuzzer_results[name] = set(results.keys())
				
				log.info("")


		log.info("Calculating unique %s in %s (%s)", coverage, fuzzers_mappings[fuzzer], fuzzer)

		fuzzer_uniques = fuzzer_results[fuzzer]

		for name in fuzzer_results.keys():
			if name == fuzzer:
				continue

			# if "/home/l.simon/CollFreeAFL/COVERAGE/xpdf-4.00/xpdf/Decrypt.cc:3360" in fuzzer_results[name]:
			# 	log.info("exist in %s", name)
			# 	if "/home/l.simon/CollFreeAFL/COVERAGE/xpdf-4.00/xpdf/PDFDoc.cc:3968" not in fuzzer_results[name]:
			# 		log.info(" but not second")
			fuzzer_uniques -= fuzzer_results[name]

		log.info("Unique %s: %u", coverage, len(fuzzer_uniques))

		fuzzer_uniques_ordered = sorted(fuzzer_uniques)
		if verbose:
			for line in fuzzer_uniques_ordered:
				print line, project_stability[fuzzer][coverage][line]


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