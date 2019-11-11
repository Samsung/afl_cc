import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)
import argparse, traceback, sys, errno
from python_libs import *
from datetime import timedelta
import time

log = logger.get(__name__)

def readOptions():
	parser = argparse.ArgumentParser()
	parser.add_argument('-p', "--project", required=True, help="Folder with all run configurations")
	parser.add_argument('-n', "--nruns", type=int, required=True, help="Minimum number of runs")
	parser.add_argument('-t', "--time", type=int, required=False, default=24, help="Time fuzzers are expected to have run (hrs)")
	parser.add_argument('-s', "--sampling", type=int, required=False, default=1800, help="Granularity/sampling of result (sec)")
	parser.add_argument('-i', "--ignore", required=False, help="List of project names to ignore", type=str)
	parser.add_argument('-o', "--only", required=False, help="List of projects to process", type=str)
	args = parser.parse_args()
	if args.ignore:
		args.ignore = [item for item in args.ignore.split(',')]
	if args.only:
		args.only = [item for item in args.only.split(',')]
	return args

class ProjectTrend:

	def __init__(self, name, plot_files, outfolder):
		self._name = name
		self._plot_files = plot_files
		self._outfolder = outfolder
		self._timestamps = []
		self._n_edges = []
		self._results = []

	def __str__(self):
		return "AFL Project trend"

	def get_name(self):
		return self._name

	def get_plot_files(self):
		return self._plot_files

	def get_outfolder(self):
		return self._outfolder

	def get_n_edges(self):
		return self._n_edges

	def get_timestamps(self):
		return self._timestamps

	def set_timestamps(self, ts):
		self._timestamps = ts

	def set_n_edges(self, n_edges):
		self._n_edges = n_edges

	def set_results(self, results):
		self._results = results

	def get_results(self, results):
		return self._results


def get_files(folder, match, file_match = True):
	ret_arr = []
	for root, dirs, files in os.walk(folder):
		if file_match or os.path.basename(root) == match:
			for fn in files: 
				if not file_match or fn == match:
					ret_arr.append(os.path.join(root,fn))
	return ret_arr


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

		#print f
		frun_path = os.path.join(project_path, f)
		project_runs = [ff for ff in os.listdir(frun_path) if (storage.isdir(os.path.join(frun_path, ff)) and ff != "outcov" and ff != "incov")]
		
		#print len(project_runs)		
		if not len(project_runs) == 3:
			raise Exception("Run " + f + " has " + str(len(project_runs)) + " fuzzers !? ")
		#print " ", project_runs
		# need to pull out the data
		AllPlotFiles = []
		for p in project_runs:
			listPlotData = get_files(os.path.join(frun_path, p), "plot_data")
			# hack to only consider original AFL folders using 64k shared bitmap
			AllPlotFiles += [f for f in listPlotData if -1==os.path.dirname(f).find("normalize") and -1==os.path.dirname(f).find("collision")] 

		# create trend dir
		trendout = os.path.join(frun_path, "trendout")
		project_trend = ProjectTrend(f, AllPlotFiles, trendout)
		project_data.append(project_trend)

	return project_data



def extract_from_line(line):
	items = line.split(",")
	if len(items) != 11:
		raise ValueError("Invalid number of items in file '" + file + "' lien : '" + line + "'")

	timestamp = int(items[0])
	n_edges = int(items[3])

	return timestamp, n_edges


def process_project(runs, min_runs, sampling, hrs_time):

	for r in runs:
		plot_files = r.get_plot_files()
		outfolder = r.get_outfolder()
		run_name = r.get_name()

		times = []
		edges = []
		for file in plot_files:
			# read file and record timestampt and number of edges found
			with open(file) as f:
				lines = f.readlines()
			if not lines:
				raise ValueError("No line found in '" + file + "'")

			if lines[0] != "# unix_time, cycles_done, cur_path, paths_total, pending_total, pending_favs, map_size, unique_crashes, unique_hangs, max_depth, execs_per_sec\n":
				raise ValueError("Invalid first line of '" + file + "' : '" +  lines[0] + "'")

			start = lines[1]
			end = lines[-1]

			start_timestamp, start_n_edges = extract_from_line(start)
			end_timestamp, end_n_edges = extract_from_line(end)
			
			# run should be 24 hours, minus a little bit (1mn) to account for startup time of AFL
			total_sec = end_timestamp - start_timestamp
			if not total_sec >= 3600*hrs_time - 60:
				raise ValueError("Run not long enough for " + run_name + " file '" + file + "'")

			# now let's extract the data
			ts = []
			es = []
			prev = start_timestamp

			# add the first
			ts.append(0)
			es.append(start_n_edges)

			for i, line in zip(range(0, len(lines[2:-1])), lines[2:-1]):
				t, e = extract_from_line(line)
				normalized_t = t-start_timestamp
				if (t-prev) > sampling:
					prev = t
					ts.append(normalized_t)
					es.append(e)

			# add the last
			ts.append(total_sec)
			es.append(end_n_edges)
			# print ts
			# print es

			times.append(ts)
			edges.append(es)

		assert(len(times) == len(edges))

		# truncate list to the same sizes
		lengths = [len(e) for e in times]
		m = min(lengths)
		
		for i in xrange(len(times)):
			times[i] = times[i][:m]
			edges[i] = edges[i][:m]

		# record the data in the project
		the_edges = edges[0][:] # copy
		for i in xrange(1, len(edges)):
			for j in xrange(len(edges[i])):
				elt = edges[i][j]
				the_edges[j] = ((the_edges[j] * i) + elt)/float(i+1)

		r.set_timestamps(times[0]) # use one of the fuzzers only
		r.set_n_edges(the_edges)

# run_trend.py -p eve/ -n 10 -s 3600
# WARNING: this assumes all the "original" builds run are the same
# this is NOT the case of qsym.vanilla though: need to normalize it
def main(options):
	
	# get the params
	project_folder = os.path.realpath( os.path.expanduser(args.project) )
	min_runs = args.nruns
	sampling = args.sampling
	time = args.time
	ignore_list = args.ignore if args.ignore != None else []
	only_list = args.only if args.only != None else []
	
	if only_list and ignore_list:
		raise ValueError("-o and -i are mutually exclusive")
	"""
		project/
			o-o-o-none/
			o-n-c-blab/
			etc/
	"""
	subfolders = [fn for fn in os.listdir(project_folder) if storage.isdir(os.path.join(project_folder, fn)) if fn != "in" and not fn.startswith("__results_")]
	log.info("Found %u projects", len(subfolders))

	# read data. This will bail out if something is wrong
	projects_data = []
	for fn in subfolders:
		if (fn in ignore_list) or (only_list and fn not in only_list):
			log.info("Ignoring project %s", fn)
			continue
		else:
			log.info("Reading project %s", fn)
		project_runs = read_project(os.path.join(project_folder, fn), min_runs)
		project_data = dict()
		project_data["name"] = fn
		project_data["runs"] = project_runs
		projects_data.append(project_data)


	log.info("\n")

	# everything is OK, now run the project
	for i, project in zip(range(len(projects_data)), projects_data):
		log.info("Processing %s", project["name"])
		process_project(project["runs"], min_runs, sampling, time)

		means = [] 
		stds = [] 
		tops = [] 
		bottoms = []
		length = min( [len(e.get_n_edges()) for e in project["runs"]] )
		for i in xrange(length):
			elts_i = [elt.get_n_edges()[i] for elt in project["runs"]]
			mean_i, std_i, top_i, bottom_i = statistics.mean_std_confidence_interval(elts_i)
			means.append(mean_i)
			stds.append(std_i)
			tops.append(top_i)
			bottoms.append(bottom_i)
		
		project["results"] = {"name": project["name"], "times": project["runs"][0].get_timestamps()[:length], "means": means, "stds": stds, "tops": tops, "bottoms": bottoms}

		storage.save_object(os.path.join(project_folder, project["name"], "trend.pik"), project["results"])

		# for project_run in project["runs"]:
		# 	storage.new_mkdir(project_run.get_outfolder())
		# 	storage.save_object(os.path.join(project_run.get_outfolder(), "trend.pik"), project_run)

		log.info("\n")


		

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
