import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)
import argparse, traceback, sys, errno
import ConfigParser
from python_libs import *
from datetime import timedelta
import time

log = logger.get(__name__)

def readOptions():
	parser = argparse.ArgumentParser()
	parser.add_argument('-e1', "--exec1", required=True, help="Executable 1")
	parser.add_argument('-e2', "--exec2", required=True, help="Executable 2")
	parser.add_argument('-d', "--dictionary", dest='dictionary', action='store_true')
	parser.add_argument('-n', "--nruns", type=int, required=True, help="Number of runs")
	parser.add_argument('-t', "--timeout", type=int, required=True, help="Max duration of each run (sec)")
	parser.add_argument('-f', "--fuzzer", required=True, help="AFL fuzzer excutable")
	parser.add_argument('-a', "--args", required=True, help="Arguments to the binary")
	parser.add_argument('-i', "--infolder", required=True, help="Folder containing input")
	args = parser.parse_args()
	return args

# Note: AFL was modified to exit as soon as a crash is found:
# function save_if_interesting() case FAULT_CRASH: exit(0)
def run_cmd(cmd, nruns, not_found, out_folder):
	results = []
	for i in xrange(nruns):
		log.info(" Run %u" % i)
		storage.removedir(out_folder)
		start_time = time.time()
		ret, stdout, stderr = run.execute_cmd_err(cmd)
		if ret != 0 and "Testing aborted by user" not in stdout:
			raise Exception("Fail: %u" % ret)
		end_time = time.time()
		elapsed_time = end_time - start_time
		log.info(" Finished at %s. Took %s", time.asctime( time.localtime(end_time)), str(timedelta(seconds=elapsed_time)))
		if ret == 0:
			results.append(int(elapsed_time))
		else:
			results.append(not_found)
	return results

def main(options):
	
	# get the params
	afl_fuzzer = os.path.realpath( os.path.expanduser(args.fuzzer) )
	nruns = args.nruns
	timeout = args.timeout
	exec1 = os.path.realpath( os.path.expanduser(args.exec1) )
	exec2 = os.path.realpath( os.path.expanduser(args.exec2) )
	infolder = os.path.realpath( os.path.expanduser(args.infolder) )
	dictionary = args.dictionary
	arguments = args.args.replace("\\", "")
	

	if not storage.file_exists(exec1):
		raise Exception("'{0}' cannot be found".format(exec1))

	if not storage.file_exists(exec2):
		raise Exception("'{0}' cannot be found".format(exec2))

	if not storage.file_exists(afl_fuzzer):
		raise Exception("'{0}' cannot be found".format(afl_fuzzer))

	if not storage.dir_exists(infolder):
		raise Exception("'{0}' cannot be found".format(infolder))

	out_folder = os.path.realpath( os.path.expanduser("out") )
	dict1 = "-x %s.dict" % exec1 if dictionary else ""
	dict2= "-x %s.dict" % exec2 if dictionary else ""
	cmd1 = "timeout %us %s %s -m none -i %s -o out %s %s @@" % (timeout, afl_fuzzer, dict1, infolder, exec1, arguments)
	cmd2 = "timeout %us %s %s -m none -i %s -o out %s %s @@" % (timeout, afl_fuzzer, dict2, infolder, exec2, arguments)

	log.info(cmd1)
	# start with first executable
	results = dict()
	results["exec1"] = []
	results["exec2"] = []

	log.info("Processing %s", os.path.basename(exec1))
	results["exec1"] = run_cmd(cmd1, nruns, -1, out_folder)

	log.info("Processing %s", os.path.basename(exec2))
	results["exec2"] = run_cmd(cmd2, nruns, -1, out_folder)

	print os.path.basename(exec1)
	print results["exec1"]

	print os.path.basename(exec2)
	print results["exec2"]

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
