import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)

from multiprocessing import Process, Value, Lock
import errno
import subprocess
import shlex
import traceback

import logger

log = logger.get(__name__)

# this executes command cmd and returns *only* if it is executed successfully
def execute_live_stdout(cmd):
	# this ouputs stdout by default, since we're not doing any redirection
	proc = subprocess.Popen(shlex.split(cmd))
	do_exit = False

	try:
		return_code = proc.wait()
		if return_code != 0:
			log.error(" >> cmd '%s' return %d", cmd, return_code)
			# error already printed thru stdout above, as we di not set stderr=subprocess.PIPE
			do_exit = True

	except:
		log.exception()

		try:
			proc.terminate()
			proc.wait()
		except:
			 log.exception()
		do_exit = True

	if do_exit:
		exit(-1)

# this executes command cmd and returns *only* if it is executed successfully
def execute_no_stdout(cmd):
	proc = subprocess.Popen(shlex.split(cmd), stderr=subprocess.PIPE, stdout=subprocess.PIPE)
	# we *must* use communicate() instead of wait() if we use PIPE redirection above
	# see https://docs.python.org/2/library/subprocess.html
	do_exit = False

	try:
		stdoutdata, stderrdata = proc.communicate()
		return_code = proc.returncode
		#print " >> cmd '" + CMD + "' return " + str(return_code)
		if return_code != 0:
			log.error(" >> cmd '%s' return %u", cmd, return_code)
			log.error(" >> strerr: %s", stderrdata)
			log.error(" >> stdout: %s", stdoutdata)
			do_exit = True

	except:
		log.exception()

		try:
			proc.terminate()
			proc.wait()
		except:
			log.exception()

		do_exit = True

	if do_exit:
		exit(-1)

def execute_cmd(cmd, quiet):
	if quiet:
		execute_no_stdout(cmd)
	else:
		execute_live_stdout(cmd)


def execute_cmd_err(cmd):
	proc = subprocess.Popen(shlex.split(cmd), stderr=subprocess.PIPE, stdout=subprocess.PIPE)
	# we *must* use communicate() instead of wait() if we use PIPE redirection above
	# see https://docs.python.org/2/library/subprocess.html
	try:
		stdoutdata, stderrdata = proc.communicate()
		return_code = proc.returncode
		#print " >> cmd '" + CMD + "' return " + str(return_code)
	except:
		log.exception()
		proc.terminate()
		proc.wait()
		raise
	
	return return_code, stdoutdata, stderrdata

# use of lock/value https://eli.thegreenplace.net/2012/01/04/shared-counter-with-pythons-multiprocessing
# https://helloacm.com/execute-external-programs-the-python-ways/
# run the commands in different processes, return 0 or -1. No stdout
# WARNING: assume stderr messages not too long
def run_multiple_commands(commands, ret, lock):
	from subprocess import Popen, PIPE
	import time
	running_procs = []
	retcode = -1
	error = False

	with lock:
		ret.value = retcode

	try:

		# WARNING: cannot use PIPE redirection along with poll/wait(), deadlock...
		# see https://docs.python.org/2/library/subprocess.html
		with open("/dev/null") as fnull:
			# WARNING: this assumes stderr does not contain too much data, or this will dead lock
			running_procs = [ (" ".join(cmd), Popen(cmd, stderr=PIPE, stdout=fnull)) for cmd in commands]
			while not error and running_procs:
				for (cmdline, proc) in running_procs:
					retcode = proc.poll()
					terminated = True if retcode is not None else False
					if terminated: # Process finished.
						
						# communicate() will return non empty tuple only if stdin/stdout is redirected through PIPE
						(stdoutdata, stderrdata) = proc.communicate()
						assert(retcode == proc.returncode)
						error = True if retcode != 0 else False
						if error:
							# Note: stdoutdata will be None because not redirected through a PIPE
							log.error("cmdline: %s", cmdline)
							log.error(" > error: %d", retcode)
							#print " > stdoutdata:", stdoutdata
							log.error(" > stderrdata: %s", stderrdata)
						running_procs.remove((cmdline, proc))
						break
				else: # No process is done, wait a bit and check again.
					#exit(-1)
					time.sleep(1)
					continue

	except:
		log.exception("run_multiple_commands: exception caught")
		#traceback.print_exc(file=sys.stdout)
		
	
	# sanity check the error code
	if error:
		assert(retcode != 0)

	# get error code
	with lock:
		ret.value = retcode

	# kill any process that may still be running
	for (_, proc) in running_procs:
		# use try/catch because some processes may finish before
		# we exit the loop above -- example when we get an error
		try:
			proc.terminate()
			proc.wait()
		except:
			log.exception()



def run_commands(commands):
	ret = Value('i', -1)
	try:
		lock = Lock()
		p = Process(target=run_multiple_commands, args=(commands, ret, lock))
		p.start()
		p.join()

	except:
		log.exception("run_commands: exception caught")
		#traceback.print_exc(file=sys.stdout)
		
	return ret.value