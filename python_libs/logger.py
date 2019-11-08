import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)

import logging
logging.basicConfig(format='%(levelname)s:\t%(message)s')

# https://docs.python.org/2/howto/logging-cookbook.html
def get(name):
	logger = logging.getLogger(name)
	logger.setLevel(logging.INFO)
	return logger