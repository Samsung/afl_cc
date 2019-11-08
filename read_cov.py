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
	parser.add_argument('-n', "--nruns", type=int, required=True, help="Minimum number of runs")
	parser.add_argument('-i', "--ignore", required=False, help="List of project names to ignore", type=str)
	parser.add_argument('-o', "--only", required=False, help="List of projects to process", type=str)
	args = parser.parse_args()
	if args.ignore:
		args.ignore = [item for item in args.ignore.split(',')]
	if args.only:
		args.only = [item for item in args.only.split(',')]
	return args


class ProjectRun:

	def __init__(self, name, seeds, infolder, outfolder):
		self._name = name
		self._seeds = seeds
		self._infolder = infolder
		self._outfolder = outfolder

	def __str__(self):
		return "AFL Project run"

	def get_name(self):
		return self._name

	def get_seeds(self):
		return self._seeds

	def get_infolder(self):
		return self._infolder

	def get_outfolder(self):
		return self._outfolder


def read_project(project_path, min_runs):

	"""
	project/
		X-Y/
			s1/queue
			s2/queue
			s3/queue
		Z-W/
			s1/queue
			s2/queue
			s3/queue
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
		
		# create in and out directories
		incov = os.path.join(frun_path,"incov")
		outcov = os.path.join(frun_path,"outcov")

		if not storage.dir_exists(outcov):
			raise Exception("Cannot find '"+ outcov +"'")

		if not storage.dir_exists(incov):
			raise Exception("Cannot find '"+ incov +"'")
			
		project_run = ProjectRun(f, None, incov, outcov)
		project_data.append(project_run)

	return project_data



def extract_project(runs, min_runs, c2sfile):

	assert(len(runs) >= min_runs)

	for r in runs:
		outfolder = r.get_outfolder()
		run_name = r.get_name()
		log.info(" %s", run_name)
		# print "", outfolder
		# continue

		bbfile = os.path.join(outfolder, "coverage_bitmap")
		edgefile = os.path.join(outfolder, "fuzz_bitmap")

		# extract the coverage results
		results = coverage.extract_coverage(c2sfile, bbfile, edgefile)

		# save the data
		storage.save_object(os.path.join(outfolder, "coverage.pik"), results)

		# print the results
		totalLines = results.getTotalLines()
		lineSet = results.getCoveredLines()
		bbSet = results.getCoveredBBs()
		edgeSet = results.getCoveredEdges()
		
		assert( len(totalLines) >= len(lineSet) )
		
		log.info("Unique lines in project: %u", len(totalLines))
		log.info("Lines visited: %u, %.6f", len(lineSet), len(lineSet)/float(len(totalLines)))
		log.info("BB visited: %u", len(bbSet))
		log.info("Edges visited: %u", len(edgeSet))


def main(options):
	
	project_folder = os.path.realpath( os.path.expanduser(args.project) )
	min_runs = args.nruns
	ignore_list = args.ignore if args.ignore != None else []
	only_list = args.only if args.only != None else []

	if not storage.dir_exists(project_folder):
		raise Exception("Cannot find '" + project_folder + "'")

	c2sfiles = storage.file_endswith(project_folder, ".c2s")
	if len(c2sfiles) == 0:
		raise Exception("Could not find .c2s file")
	if len(c2sfiles) > 1:
		raise Exception("Too many .c2s files")
	c2sfile = c2sfiles[0]

	subfolders = [fn for fn in os.listdir(project_folder) if storage.isdir(os.path.join(project_folder, fn)) if fn != "in" and not fn.startswith("__results_")]
	log.info("Found %u projects", len(subfolders))

	# read project and put them in our list
	projects_data = []
	for fn in subfolders:
		if (fn in ignore_list) or (only_list and fn not in only_list):
			log.info("Ignoring project %s", fn)
			continue
		log.info("Reading project %s", fn)
		project_runs = read_project(os.path.join(project_folder, fn), min_runs)
		project_data = dict()
		project_data["name"] = fn
		project_data["runs"] = project_runs
		projects_data.append(project_data)	
	

	log.info("\n")

	# extract coverage data from the project
	for project in projects_data:
		log.info("Processing %s", project["name"])
		extract_project(project["runs"], min_runs, c2sfile)
		log.info("\n")
		

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