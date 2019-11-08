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
	parser.add_argument('-x', "--xaxis", required=True, help="X-axis")
	parser.add_argument('-y', "--yaxis", required=True, help="Y-axis")
	parser.add_argument('-d', "--data", required=True, help="List of integers", type=str)
	parser.add_argument('-o', "--ofile", required=False, help="Output file")
	args = parser.parse_args()
	args.data = [int(item) for item in args.data.split(',')]
	return args

def do_hist_single(y,err,nbins):
	weights = np.ones_like(y)/(float)(len(y))
	#n, bins, patches = plt.hist(y, bins=nbins, normed=False, histtype='step', cumulative=True, weights=weights, color='#089FFF')
	n, bins, patches = plt.hist(y, nbins, normed=True, facecolor='#089FFF', alpha=0.5, hatch='/\\')
	# plt.fill_between(bins[:-1], n-err, n+err,
	# 	alpha=0.2, edgecolor='#1B2ACC', facecolor='#089FFF',
	# 	linewidth=0, linestyle='dashdot', antialiased=True)
    
	return (n, bins, patches)


def main(options):
	
	# get the params
	xaxis = args.xaxis
	yaxis = args.yaxis
	data = args.data
	ofile = args.ofile


	fig, ax = plt.subplots()
	ax.spines['top'].set_visible(False)	# dont display top border
	ax.spines['right'].set_visible(False)	# dont display right border
	ax.yaxis.tick_left()# dont display right ticks
	ax.xaxis.tick_bottom()# dont display top ticks
	ax.set_autoscaley_on(False)
	ax.set_xlim([0,30])

	# colors = ['#089FFF','#FF9848']
	# edgecolors = ['#1B2ACC','#CC4F1B']
	# facecolors=['#089FFF', '#FF9848']

	Xrange = np.arange(0, len(data), 1)


	values, bins, patches = plt.hist(data, Xrange, normed=True, facecolor='#089FFF', alpha=0.5, hatch='/\\')
	cumulative = np.cumsum(values)
	plt.plot(bins[:-1], cumulative, 'k--')

	ax.set_ylim([0,min(1,mt.ceil(max(cumulative)*10)/10)])
	ax.grid()
	ax.set_ylabel(yaxis);
	ax.set_xlabel(xaxis)
	if ofile:
		plt.savefig(ofile, transparent = True, bbox_inches='tight')
	else:
		plt.show()
	plt.close()


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