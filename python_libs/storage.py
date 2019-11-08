import cPickle as pickle
import os, glob
import errno

def save_object(fn, data):
	with open(fn, "wb") as output_file:
		pickle.dump( data, output_file )

def read_object(fn):
	with open(fn, "rb") as input_file:
   		return pickle.load(input_file)

def read_file(fn, byline):
	if byline:
		with open(fn, 'r') as f:
			return f.readlines()
	else:
		with open(fn, 'rb') as f:
			return f.read()
	return None

def removedir(thedir):
	if os.path.isdir(thedir):
		import shutil
		shutil.rmtree(thedir, ignore_errors=True)


def silentmkdir(dirname):
	try:
		os.makedirs(dirname)
	except OSError as e:
		if e.errno != errno.EEXIST:
			raise

def new_mkdir(dirname):
	removedir(dirname)
	silentmkdir(dirname)

def silent_rmfile(filename):
	try:
		os.remove(filename)
	except OSError as e: # this would be "except OSError, e:" before Python 2.6
		if e.errno != errno.ENOENT: # errno.ENOENT = no such file or directory
			raise # re-raise exception if a different error occurred

def file_endswith(infolder, end):
	allFiles = []
	os.chdir(infolder)
	return glob.glob("*" + end)

def isdir(fn):
	return os.path.isdir(fn)

def dir_exists(fn):
	return isdir(fn)

def isfile(fn):
	return os.path.isfile(fn)

def file_exists(fn):
	return isfile(fn)

