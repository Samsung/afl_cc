import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)
import argparse, traceback, sys, errno
from python_libs import *

log = logger.get(__name__)

def readOptions():
	parser = argparse.ArgumentParser()
	parser.add_argument('-p', "--project", required=True, help="Folder with all run configurations")
	parser.add_argument('-b', "--baseline", required=True, help="Baseline project name")
	parser.add_argument(      "--no-percent", action="store_true", help="Use number and not percentage")
	parser.add_argument('-n', "--nruns", type=int, required=True, help="Minimum number of runs")
	parser.add_argument('-c', "--cummulative", required=False, action="store_true", help="Cummulative (default is comparison for each run)")
	parser.add_argument('-i', "--ignore", required=False, help="List of project names to ignore", type=str)
	parser.add_argument('-o', "--only", required=False, help="List of projects to process", type=str)
	args = parser.parse_args()
	if args.ignore:
		args.ignore = [item for item in args.ignore.split(',')]
	if args.only:
		args.only = [item for item in args.only.split(',')]
	return args
choices=['servers', 'storage', 'all']

# class ProjectRun:

# 	def __init__(self, name, seeds, infolder, outfolder):
# 		self._name = name
# 		self._seeds = seeds
# 		self._infolder = infolder
# 		self._outfolder = outfolder

# 	def __str__(self):
# 		return "AFL Project run"

# 	def get_name(self):
# 		return self._name

# 	def get_seeds(self):
# 		return self._seeds

# 	def get_infolder(self):
# 		return self._infolder

# 	def get_outfolder(self):
# 		return self._outfolder


def read_project(project_path, min_runs):

	"""
	project/
		X-Y/outcov/coverage.pik
		Z-W/outcov/coverage.pik
	"""
	project_data = []
	folder_runs = [ff for ff in os.listdir(project_path) if storage.isdir(os.path.join(project_path, ff))]
	if not len(folder_runs) >= min_runs:
		raise Exception("Project " + os.path.basename(project_path) + " has only " + str(len(folder_runs)) + " runs < minmum request " + str(min_runs))
	
	for f in folder_runs:

		frun_path = os.path.join(project_path, f)
		project_runs = [ff for ff in os.listdir(frun_path) if (storage.isdir(os.path.join(frun_path, ff)) and ff != "outcov" and ff != "incov")]

		if not len(project_runs) == 3:
			raise Exception("Run " + f + " has " + str(len(project_runs)) + " fuzzers !? ")
		
		# create out directories
		outcov = os.path.join(frun_path,"outcov")

		if not storage.dir_exists(outcov):
			raise Exception("Cannot find '"+ outcov +"'")

		coverage_pik = os.path.join(outcov, "coverage.pik")
		if not storage.file_exists(coverage_pik):
			raise Exception("Cannot find '"+ coverage_pik +"'")

		project_data.append((f, coverage_pik))

	return project_data



def stats_project(runs, min_runs):

	assert(len(runs) >= min_runs)

	lines = []
	edges = []
	bbs = []
	totalLines = None

	for r in runs:
		coverage_pik = r[1]
		run_name = r[0]
		log.info(" %s", run_name)

		obj = storage.read_object(coverage_pik)
		lines.append(obj.getCoveredLines())
		edges.append(obj.getCoveredEdges())
		bbs.append(obj.getCoveredBBs())
		if totalLines == None:
			totalLines = obj.getTotalLines()
		else:
			assert(totalLines == obj.getTotalLines())
	
	return totalLines, lines, edges, bbs


def compute_sets(s1, s2):
	return s1 - s2, s2 - s1

def print_record_result(header, res_only_mean, res_only_std, res_only_upper, res_only_lower, baseline_only_mean, baseline_only_std, baseline_only_upper, baseline_only_lower, baseline, current, thestats, project_comparisons):
	# print
	print header, "baseline:", baseline
	print header, "current:", current
	print header, "raw difference:", res_only_mean, res_only_std, res_only_upper, res_only_lower
	print header, "percent difference: %.6f %.6f %.6f %.6f" % (res_only_mean/baseline, res_only_std/baseline, res_only_upper/baseline, res_only_lower/baseline)
	print header, "missed:", baseline_only_mean, baseline_only_std, baseline_only_upper, baseline_only_lower
	print header, "percent missed: %.6f %.6f %.6f %.6f" % (baseline_only_mean/baseline, baseline_only_std/baseline, baseline_only_upper/baseline, baseline_only_lower/baseline)
	print header, "Significance:", thestats

	# record
	project_comparisons[header] = ((float(res_only_mean)/baseline, float(res_only_std)/baseline), (float(baseline_only_mean)/baseline, float(baseline_only_std)/baseline))

def print_record_result_cummulative(header, baseline, res_only, baseline_only, project_comparisons, do_percent):
	# print
	print header, "baseline: %u" % baseline
	FmtStr =  "%.6f -> "
	if do_percent:
		FmtStr += "%.6f%%" 
	else:
		FmtStr += "%.6f (no_percent)"

	NewStr = "new visited: " + FmtStr
	MissedStr = "missed: " + FmtStr
	print header, NewStr % (res_only, float(res_only)/(baseline if do_percent else 1))
	print header, MissedStr %(baseline_only, float(baseline_only)/(baseline if do_percent else 1))

	# record
	project_comparisons[header] = ((float(res_only)/(baseline if do_percent else 1), None), (float(baseline_only)/(baseline if do_percent else 1), None))

def get_stability2(Elements):
	eltDict = dict()
	nb_runs = len(Elements)
	ret = dict()
	for lset in Elements:
		for l in lset:
			if l not in eltDict:
				eltDict[l] = 1
			else:
				eltDict[l] += 1

	upper = True
	for elt_name, elt_num in eltDict.iteritems():
		occurrence = float(elt_num)/nb_runs*10.
		assert(occurrence > 0)
		# check if occurrence is not a int and is greater than 0 - we don't care about unvisited lines
		if occurrence != int(occurrence):
			# if the value is not an integer, pick the lower or upper value with p=0.5
			if upper:
				occurrence = int(occurrence)+1
				upper = False
			else:
				occurrence = int(occurrence) if occurrence > 1 else int(occurrence)+1
				upper = True if occurrence > 1 else False

		assert (elt_name not in ret)
		ret[elt_name] = occurrence
	return ret

def get_stability(Elements):
	eltDict = dict()
	nb_runs = len(Elements)
	ret = {1:0, 2:0, 3:0, 4:0, 5:0, 6:0, 7:0, 8:0, 9:0, 10:0}
	for lset in Elements:
		for l in lset:
			if l not in eltDict:
				eltDict[l] = 1
			else:
				eltDict[l] += 1

	upper = True
	for elt_name, elt_num in eltDict.iteritems():
		occurrence = float(elt_num)/nb_runs*10.
		assert(occurrence > 0)
		# check if occurrence is not a int and is greater than 0 - we don't care about unvisited lines
		if occurrence != int(occurrence):
			# if the value is not an integer, pick the lower or upper value with p=0.5
			if upper:
				occurrence = int(occurrence)+1
				upper = False
			else:
				occurrence = int(occurrence) if occurrence > 1 else int(occurrence)+1
				upper = True if occurrence > 1 else False

		ret[occurrence] += 1
	return ret

def main(options):
	
	project_folder = os.path.realpath( os.path.expanduser(args.project) )
	baseline = args.baseline
	min_runs = args.nruns
	ignore_list = args.ignore if args.ignore != None else []
	only_list = args.only if args.only != None else []
	is_cummulative = args.cummulative
	no_percent = args.no_percent

	if only_list and ignore_list:
		raise Exception("-o and -i are mutually exclusive")

	if not storage.dir_exists(project_folder):
		raise Exception("Cannot find '" + project_folder + "'")


	subfolders = [fn for fn in os.listdir(project_folder) if storage.isdir(os.path.join(project_folder, fn)) if fn != "in" and not fn.startswith("__results_")]
	log.info("Found %u projects", len(subfolders))

	# outfolder, create if does not exist
	outfolder = os.path.join(project_folder, "__results_%s" % baseline)
	storage.silentmkdir(outfolder)

	log.info("Will store results under %s", outfolder)
	
	# delete previous files
	functional_pik = os.path.join(outfolder, "functional_coverage.pik")
	stability_pik = os.path.join(outfolder, "stability.pik")
	stability_pik2 = os.path.join(outfolder, "stability2.pik")
	storage.silent_rmfile(functional_pik)
	storage.silent_rmfile(stability_pik)
	storage.silent_rmfile(stability_pik2)

	# read project and put them in our list
	projects_data = []
	for fn in subfolders:
		if ((fn in ignore_list) or (only_list and fn not in only_list)) and fn != baseline:
			log.info("Ignoring project %s", fn)
			continue
		else: 
			log.info("Reading project %s", fn)

		# if fn != "o-o-o-none" and fn != "o-n-c-notdict-noopt" and fn != "o-o-o-newdict" and fn != "o-o-o-oldict":
		# 	continue
		project_runs = read_project(os.path.join(project_folder, fn), min_runs)
		project_data = dict()
		project_data["name"] = fn
		project_data["runs"] = project_runs
		projects_data.append(project_data)	
	

	print

	# extract coverage data from the project
	projects_res = dict()
	for project in projects_data:
		print
		log.info("Processing %s", project["name"])
		totalLines, lines, edges, bbs = stats_project(project["runs"], min_runs)
		projects_res[project["name"]] = {"totalLines" : totalLines, "lines" : lines, "edges" : edges, "bbs" : bbs}

	if baseline not in projects_res:
		raise Exception(baseline + " not found in projects")

	baseline_project = projects_res[baseline]
	Edges_baseline = baseline_project["edges"]
	Lines_baseline = baseline_project["lines"]
	Bbs_baseline = baseline_project["bbs"]
	totalLines_baseline = baseline_project["totalLines"]

	# we have the info, compare to the baseline
	print "Unique lines:", len(totalLines_baseline)

	'''
		project_stability[project_name]["Lines"][bucket0, bucket1, etc , bucket10]
		project_stability[project_name]["BBs"][bucket0, bucket1, etc , bucket10]
		project_stability[project_name]["Edges"][bucket0, bucket1, etc , bucket10]
	'''
	project_stability = dict()
	for key, res in projects_res.iteritems():
		# set of all covered BBs, Edges, Lines
		Lines = res["lines"]
		Edges = res["edges"]
		Bbs = res["bbs"]

		#print key, len(Lines)
		assert(len(Lines) == len(Edges) and len(Lines) == len(Bbs))
		
		allVisitedLines = set([e for l in Lines for e in l])
		allVisitedEdges = set([e for l in Edges for e in l])
		allVisitedBBs = set([e for l in Bbs for e in l])
		
		# now get some stats
		project_stability[key] = dict()
		project_stability[key]["allLines"] = len(allVisitedLines)
		project_stability[key]["allBBs"] = len(allVisitedBBs)
		project_stability[key]["allEdges"] = len(allVisitedEdges)
		project_stability[key]["Lines"] = get_stability(Lines)
		project_stability[key]["Edges"] = get_stability(Edges)
		project_stability[key]["BBs"] = get_stability(Bbs)


	storage.save_object(stability_pik, project_stability)

	'''
		project_stability[project_name]["Lines"][l1, l2, etc , lN]
		project_stability[project_name]["BBs"][bb1, bb2, etc , bbN]
		project_stability[project_name]["Edges"][e1, e2, etc , eN]
	'''

	project_stability2 = dict()
	for key, res in projects_res.iteritems():
		# set of all covered BBs, Edges, Lines
		Lines = res["lines"]
		Edges = res["edges"]
		Bbs = res["bbs"]

		#print key, len(Lines)
		assert(len(Lines) == len(Edges) and len(Lines) == len(Bbs))
		
		allVisitedLines = set([e for l in Lines for e in l])
		allVisitedEdges = set([e for l in Edges for e in l])
		allVisitedBBs = set([e for l in Bbs for e in l])
		
		# now get some stats
		project_stability2[key] = dict()
		project_stability2[key]["allLines"] = len(allVisitedLines)
		project_stability2[key]["allBBs"] = len(allVisitedBBs)
		project_stability2[key]["allEdges"] = len(allVisitedEdges)
		project_stability2[key]["Lines"] = get_stability2(Lines)
		project_stability2[key]["Edges"] = get_stability2(Edges)
		project_stability2[key]["BBs"] = get_stability2(Bbs)

	storage.save_object(stability_pik2, project_stability2)

	''' for each project, we will have:
		project_comparisons[project_name]["Lines"] = ((positive_mean, positive_std), (negative_mean, negative_std)
		project_comparisons[project_name]["BBs"] = ((positive_mean, positive_std), (negative_mean, negative_std)
		project_comparisons[project_name]["Edges"] = ((positive_mean, positive_std), (negative_mean, negative_std)
		
	'''
	project_comparisons = dict()
	for key, res in projects_res.iteritems():
		if key == baseline:
			continue

		totalLines = res["totalLines"]
		# if totalLines != totalLines_baseline:
		# 	print len(totalLines), len(totalLines_baseline)
		# 	for i in xrange(len(totalLines)):
		# 		if totalLines[i] != totalLines_baseline[i]:
		# 			print i, totalLines[i], totalLines_baseline[i]
			#print totalLines, totalLines_baseline
		assert(totalLines == totalLines_baseline)

		Lines = res["lines"]
		Edges = res["edges"]
		Bbs = res["bbs"]
		
		Edges_baseline_only = []
		Lines_baseline_only = []
		Bbs_baseline_only = []
		Edges_res_only = []
		Lines_res_only = []
		Bbs_res_only = []

		baseline_edges = []
		baseline_bbs = []
		baseline_lines = []

		project_lines = []
		project_edges = []
		project_bbs = []

		#assert(len(Lines) == len(Edges) and len(Edges) == len(Bbs) and len(Bbs) == len(Lines_baseline) and len(Lines_baseline) == len(Edges_baseline) and len(Edges_baseline) == len(Bbs_baseline))
		allSame = (len(Lines) == len(Edges) and len(Edges) == len(Bbs) and len(Bbs) == len(Lines_baseline) and len(Lines_baseline) == len(Edges_baseline) and len(Edges_baseline) == len(Bbs_baseline))
		if not allSame:
			# this code is only here because I had to stop an experiment early...
			# if not for this, just uncomment the assert() above and comment the rest
			log.info("Not all fuzzers have same number of run")
			assert(len(Lines) == len(Edges) and len(Edges) == len(Bbs))
			assert(len(Lines_baseline) and len(Lines_baseline) == len(Edges_baseline) and len(Edges_baseline) == len(Bbs_baseline))
			the_min = min(len(Lines), len(Lines_baseline))

			log.info("Using only %u runs", the_min)

			Lines = Lines[:the_min]
			Edges = Edges[:the_min]
			Bbs = Bbs[:the_min]
			Lines_baseline = Lines_baseline[:the_min]
			Edges_baseline = Edges_baseline[:the_min]
			Bbs_baseline = Bbs_baseline[:the_min]

		# for cummulative stats
		cummulative_baseline_edges = set()
		cummulative_baseline_bbs = set()
		cummulative_baseline_lines = set()
		cummulative_res_edges = set()
		cummulative_res_bbs = set()
		cummulative_res_lines = set()


		if is_cummulative:

			for lines, edges, bbs, lines_baseline, edges_baseline, bbs_baseline in zip(Lines, Edges, Bbs, Lines_baseline, Edges_baseline, Bbs_baseline):
			
				# print "cur lines:", len(lines)
				# print "cur lines:", len(cummulative_res_lines)

				# print "baseline lines:", len(bbs_baseline)
				# print "cur lines:", len(cummulative_baseline_lines)
				# print 
				cummulative_res_edges = edges | cummulative_res_edges
				cummulative_res_bbs = bbs | cummulative_res_bbs
				cummulative_res_lines = lines | cummulative_res_lines
				
				cummulative_baseline_edges = edges_baseline | cummulative_baseline_edges
				cummulative_baseline_bbs = bbs_baseline | cummulative_baseline_bbs
				cummulative_baseline_lines = lines_baseline | cummulative_baseline_lines

			# print len(cummulative_res_lines), len(cummulative_baseline_lines)
			# print len(cummulative_res_lines - cummulative_baseline_lines)
			# exit(-1)
			Edges_baseline_only = cummulative_baseline_edges - cummulative_res_edges
			Lines_baseline_only = cummulative_baseline_lines - cummulative_res_lines
			Bbs_baseline_only = cummulative_baseline_bbs - cummulative_res_bbs
			Edges_res_only = cummulative_res_edges - cummulative_baseline_edges
			Lines_res_only = cummulative_res_lines - cummulative_baseline_lines
			Bbs_res_only = cummulative_res_bbs - cummulative_baseline_bbs

			# baseline_edges.append(len(edges_baseline))
			# baseline_bbs.append(len(bbs_baseline))
			# baseline_lines.append(len(lines_baseline))

			# project_lines.append(len(lines))
			# project_edges.append(len(edges))
			# project_bbs.append(len(bbs))


			# now compute overlap/missed/new on the cummulative coverage
			# to keep the structure same as for non-cummulative, we create 1 single run and we put the same datat into it, with an std of None
			# so we can catch if we use a cummulative .pik to plot a non-cummulative plot
			if key not in project_comparisons:
				project_comparisons[key] = dict()
			
			print
			print "project", key
			print "========================"
			print_record_result_cummulative("Lines", len(cummulative_baseline_lines), len(Lines_res_only), len(Lines_baseline_only), project_comparisons[key], not no_percent)
			print_record_result_cummulative("Edges", len(cummulative_baseline_edges), len(Edges_res_only), len(Edges_baseline_only) ,project_comparisons[key], not no_percent)
			print_record_result_cummulative("BBs", len(cummulative_baseline_bbs), len(Bbs_res_only), len(Bbs_baseline_only), project_comparisons[key], not no_percent)


		else:
			assert (not no_percent) # not supported

			for lines, edges, bbs, lines_baseline, edges_baseline, bbs_baseline in zip(Lines, Edges, Bbs, Lines_baseline, Edges_baseline, Bbs_baseline):
				
				edges_baseline_only = []
				lines_baseline_only = []
				bbs_baseline_only = []
				edges_res_only = []
				lines_res_only = []
				bbs_res_only = []

				
				edges_res_only, edges_baseline_only = compute_sets(edges, edges_baseline)
				lines_res_only, lines_baseline_only = compute_sets(lines, lines_baseline)
				bbs_res_only, bbs_baseline_only = compute_sets(bbs, bbs_baseline)

				#print len(bbs_res_only), len(bbs_baseline), len(bbs)
				
				Edges_baseline_only.append(edges_baseline_only)
				Lines_baseline_only.append(lines_baseline_only)
				Bbs_baseline_only.append(bbs_baseline_only)
				Edges_res_only.append(edges_res_only)
				Lines_res_only.append(lines_res_only)
				Bbs_res_only.append(bbs_res_only)

				baseline_edges.append(len(edges_baseline))
				baseline_bbs.append(len(bbs_baseline))
				baseline_lines.append(len(lines_baseline))

				project_lines.append(len(lines))
				project_edges.append(len(edges))
				project_bbs.append(len(bbs))

			
			
			# now compare the sets
			edges_baseline_only_mean, edges_baseline_only_std, edges_baseline_only_upper, edges_baseline_only_lower = statistics.mean_std_confidence_interval([len(e) for e in Edges_baseline_only])
			lines_baseline_only_mean, lines_baseline_only_std, lines_baseline_only_upper,lines_baseline_only_lower = statistics.mean_std_confidence_interval([len(e) for e in Lines_baseline_only])
			bbs_baseline_only_mean, bbs_baseline_only_std, bbs_baseline_only_upper, bbs_baseline_only_lower = statistics.mean_std_confidence_interval([len(e) for e in Bbs_baseline_only])
			
			edges_res_only_mean, edges_res_only_std, edges_res_only_upper, edges_res_only_lower = statistics.mean_std_confidence_interval([len(e) for e in Edges_res_only])
			lines_res_only_mean, lines_res_only_std, lines_res_only_upper,lines_res_only_lower = statistics.mean_std_confidence_interval([len(e) for e in Lines_res_only])
			bbs_res_only_mean, bbs_res_only_std, bbs_res_only_upper, bbs_res_only_lower = statistics.mean_std_confidence_interval([len(e) for e in Bbs_res_only])

			edges_Stats = statistics.mannwhitneyu([len(e) for e in Edges_baseline_only], [len(e) for e in Edges_res_only])
			lines_Stats = statistics.mannwhitneyu([len(e) for e in Lines_baseline_only], [len(e) for e in Lines_res_only])
			bbs_Stats = statistics.mannwhitneyu([len(e) for e in Bbs_baseline_only], [len(e) for e in Bbs_res_only])

			if key not in project_comparisons:
				project_comparisons[key] = dict()

			print
			print "project", key
			print "========================"
			mean_lines = statistics.mean(project_lines)
			print "Lines visited: %u, %.6f" % (mean_lines, mean_lines/float(len(totalLines)))
			print_record_result("Lines", lines_res_only_mean, lines_res_only_std, lines_res_only_upper, lines_res_only_lower, lines_baseline_only_mean, lines_baseline_only_std, lines_baseline_only_upper, lines_baseline_only_lower, statistics.mean(baseline_lines), statistics.mean(project_lines), lines_Stats, project_comparisons[key])
			print
			print_record_result("BBs", bbs_res_only_mean, bbs_res_only_std, bbs_res_only_upper, bbs_res_only_lower, bbs_baseline_only_mean, bbs_baseline_only_std, bbs_baseline_only_upper, bbs_baseline_only_lower, statistics.mean(baseline_bbs), statistics.mean(project_bbs), bbs_Stats, project_comparisons[key])
			print
			print_record_result("Edges", edges_res_only_mean, edges_res_only_std, edges_res_only_upper, edges_res_only_lower, edges_baseline_only_mean, edges_baseline_only_std, edges_baseline_only_upper, edges_baseline_only_lower, statistics.mean(baseline_edges), statistics.mean(project_edges), edges_Stats, project_comparisons[key])
			

	storage.save_object(functional_pik, project_comparisons)


	# # extract the coverage results
	# results = coverage.extract_coverage(c2sfile, bbfile, edgefile)
	
	# # save the data
	# storage.save_object(os.path.join(project_folder, "coverage.pik"), results)

	# # print the results
	# totalLines = results.getTotalLines()
	# lineSet = results.getCoveredLines()
	# bbSet = results.getCoveredBBs()
	# edgeSet = results.getCoveredEdges()
	
	# print 
	# print "Unique lines in project:", len(totalLines)
	# print "Lines visited: %u, %.6f" % (len(lineSet), len(lineSet)/float(len(totalLines)))
	# print "BB visited:", len(bbSet)
	# print "Edges visited:", len(edgeSet)
	
	# vals1 = [9248, 9003, 9321, 9250, 8921, 9210, 9111, 9512, 8866, 9122]
	# vals2 = [8248, 8003, 8321, 8250, 7921, 8210, 8111, 8512, 7866, 8122]
	# m1, std1, upper1, lower1 = statistics.mean_std_confidence_interval(vals1)
	# print m1, std1, upper1, lower1
	# m2, std2, upper2, lower2 = statistics.mean_std_confidence_interval(vals2)
	# print m2, std2, upper2, lower2
	# z, p = statistics.ttest(vals1, vals2)
	# print z, p


if __name__ == '__main__':
		
	args = readOptions()

	try:
		main(args)
		
	except:
		print "exception caught"
		traceback.print_exc(file=sys.stdout)
	#finally:
		#print 'finally'

	print 'Done ...'