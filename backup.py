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
import shutil
import time
from datetime import timedelta

log = logger.get(__name__)

def readOptions():
	parser = argparse.ArgumentParser()
	parser.add_argument('-i', "--input", required=True, help="Input folder")
	parser.add_argument('-o', "--output", required=True, help="Output folder")
	parser.add_argument('-e', "--extensions", required=True, help="List of extensions, separated by ,")
	args = parser.parse_args()
	return args


def find_files(folder, extensions, progress_text = "", n_progress = 1):
	all_files = []
	n = 0
	for root, dirs, files in os.walk(folder):
		for file in files:
			if n == 0 and progress_text:
				sys.stdout.write(progress_text)
				sys.stdout.flush()
			n += 1
			n %= n_progress
			if file.endswith(tuple(extensions)):
				all_files.append(os.path.join(root, file))
	
	return all_files


# python backup.py -i in -o out -e .pik,.txt,.pdf
def main(options):
	
	input_folder = os.path.realpath( os.path.expanduser(args.input) )
	output_folder = os.path.realpath( os.path.expanduser(args.output) )
	extensions = [elt for elt in args.extensions.split(",") if len(elt)>0]

	if not extensions:
		raise ValueError("Invalid extensions")

	if output_folder.startswith(input_folder):
		raise ValueError("Output folder cannot be inside input folder")

	if storage.dir_exists(output_folder):
		raise ValueError("Output folder already exists")

	if not storage.dir_exists(input_folder):
		raise ValueError("Input folder does not exist")

	start_time = time.time()

	# now list all files and save them

	log.info("Starting at %s", time.asctime( time.localtime(start_time) ))

	pik_files = find_files(input_folder, extensions, ".", 100000)
	for file in pik_files:
		assert(file.startswith(input_folder))
		new_file = os.path.join(output_folder, file[len(input_folder)+1:])
		new_folder = os.path.dirname(new_file)
		storage.silentmkdir(new_folder)
		shutil.copy(file, new_file)

	end_time = time.time()
	elapsed_time = end_time - start_time
	log.info("Finished at %s. Took %s", time.asctime( time.localtime(end_time)), str(timedelta(seconds=elapsed_time)))


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