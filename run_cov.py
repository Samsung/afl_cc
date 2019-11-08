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
	parser.add_argument('-f', "--aflcov", required=True, help="AFL coverage binary")
	parser.add_argument('-a', "--args", required=True, help="Arguments to the binary. Must contain the @@ if program reads from file")
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

# returns all files under folder/***/queue
def get_seeds(folder, match):
	ret_arr = []
	for root, dirs, files in os.walk(folder):
		if os.path.basename(root) == match:
			for fn in files: 
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
	
	# print fn
	# print folder_runs
	# print

	for f in folder_runs:

		#print f
		frun_path = os.path.join(project_path, f)
		project_runs = [ff for ff in os.listdir(frun_path) if (storage.isdir(os.path.join(frun_path, ff)) and ff != "outcov" and ff != "incov")]

		#print len(project_runs)		
		if not len(project_runs) == 3:
			raise Exception("Run " + f + " has " + str(len(project_runs)) + " fuzzers !? ")
		#print " ", project_runs
		# need to pull out the data
		AllSeeds = []
		for p in project_runs:
			
			listSeeds = get_seeds(os.path.join(frun_path, p), "queue")
			AllSeeds += listSeeds

			listSeeds = get_seeds(os.path.join(frun_path, p), "hangs")
			AllSeeds += listSeeds

			listSeeds = get_seeds(os.path.join(frun_path, p), "crashes")
			AllSeeds += listSeeds

			'''
			# use the queue/ folder
			queue_folder = os.path.join(frun_path, p, "queue")
			listSeeds = [os.path.join(queue_folder, ff)  for ff in os.listdir(queue_folder) if storage.isfile(os.path.join(queue_folder, ff))]
			AllSeeds += listSeeds
			#print p, len(listSeeds)#, os.path.join(queue_folder, listSeeds[0])

			# ... and the crashes/ folder
			crash_folder = os.path.join(frun_path, p, "crashes")
			listSeeds = [os.path.join(crash_folder, ff)  for ff in os.listdir(crash_folder) if storage.isfile(os.path.join(crash_folder, ff))]
			AllSeeds += listSeeds

			# ... and the hangs/ folder
			hangs_folder = os.path.join(frun_path, p, "hangs")
			listSeeds = [os.path.join(hangs_folder, ff)  for ff in os.listdir(hangs_folder) if storage.isfile(os.path.join(hangs_folder, ff))]
			AllSeeds += listSeeds
			'''

		# create in and out directories
		incov = os.path.join(frun_path,"incov")
		outcov = os.path.join(frun_path,"outcov")

		#print "run ", len(AllSeeds), " seeds from " + incov + " and store in ", outcov

		project_run = ProjectRun(f, AllSeeds, incov, outcov)
		project_data.append(project_run)

	return project_data


def run_project(runs, min_runs, afl_fuzzer, executable, arguments):

	# get the list of commands for this project
	# each project has at leats min_runs runs
	commands = []
	for r in runs:
		seeds = r.get_seeds()
		infolder = r.get_infolder()
		outfolder = r.get_outfolder()
		run_name = r.get_name()

		log.info(" Symlinking %u files into %s", len(seeds), run_name)

		# create the input directory, symblink all files, and delete the output dir
		storage.new_mkdir(infolder)
		storage.removedir(outfolder)
		for i in range(len(seeds)):
			os.symlink(os.path.join(seeds[i]), os.path.join(infolder, str(i)))

		# create the AFL commands
		# give more timeout (4s) because of large shared map. This will not block for 4s, it will return before if process terminates
		if len(arguments):
			args = arguments.split(" ")
			commands.append([afl_fuzzer, "-t", "60+", "-i", infolder, "-o", outfolder, executable] + args)
		else:
			commands.append([afl_fuzzer, "-t", "60+", "-i", infolder, "-o", outfolder, executable])			

	# ignore crashing inputs.. . since we also us the seeds from the crashes/ folder
	# we do not need calibration, and we ignore variable edges.
	os.environ["AFL_SKIP_CRASHES"] = "1"
	os.environ["AFL_NO_CAL"] = "1"	# Note: this was added to AFL to make it fast for us to extract coverage metrics

	# now run the commands in parallel
	assert(len(commands) >= min_runs)
	start_time = time.time()
	log.info("RUNNING %s at %s ...", os.path.basename(afl_fuzzer), time.asctime( time.localtime(start_time) ))
	ret = run.run_commands(commands)

	end_time = time.time()
	elapsed_time = end_time - start_time
	log.info("Finished at %s. Took %s", time.asctime( time.localtime(end_time)), str(timedelta(seconds=elapsed_time)))

	if ret != 0:
		raise Exception("Error running commands. Return value: " + str(ret))

def main(options):
	
	# get the params
	project_folder = os.path.realpath( os.path.expanduser(args.project) )
	afl_fuzzer = os.path.expanduser(args.aflcov) # note: don't use realpath as the tool checks for its name thru symbolic link
	min_runs = args.nruns
	ignore_list = args.ignore if args.ignore != None else []
	only_list = args.only if args.only != None else []
	arguments = args.args.replace("\\", "")
	c2sfiles = storage.file_endswith(project_folder, ".c2s")

	if len(c2sfiles) == 0:
		raise Exception("Could not find .c2s file")
	if len(c2sfiles) > 1:
		raise Exception("Too many .c2s files")
	c2sfile = c2sfiles[0]

	executable = os.path.join(project_folder, c2sfile[:-4])
	
	if not storage.file_exists(executable):
		raise Exception("Executable (" + executable + ") not found")
	
	if not storage.file_exists(afl_fuzzer):
		raise Exception("AFL (" + afl_fuzzer + ") not found")

	if "@@" not in arguments:
		print "WARNING: your argument list does not contain '@@'. Is this correct?"
		print "Presse ENTER to continue..."
		value = raw_input()
		if value: # user pressn something that is not ENTER
			return

	if only_list and ignore_list:
		raise Exception("-o and -i are mutually exclusive")
	"""
		project/
			o-o-o-none/
			o-n-c-blab/
			etc/
	"""
	subfolders = [fn for fn in os.listdir(project_folder) if storage.isdir(os.path.join(project_folder, fn)) if fn != "in"]
	log.info("Found %u projects", len(subfolders))

	# read data. This will bail out if something is wrong
	projects_data = []
	for fn in subfolders:
		if fn.startswith("__results_"):
			continue
		elif (fn in ignore_list) or (only_list and fn not in only_list):
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
	for project in projects_data:
		log.info("Processing %s", project["name"])
		run_project(project["runs"], min_runs, afl_fuzzer, executable, arguments)
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
