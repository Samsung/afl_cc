import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)
import argparse, traceback, sys, errno
import ConfigParser
from python_libs import *


def readOptions():
	parser = argparse.ArgumentParser()
	parser.add_argument('-p1', "--project1", required=True, help="Out folder1 of AFL")
	parser.add_argument('-p2', "--project2", required=True, help="Out folder2 of AFL")
	args = parser.parse_args()
	return args


def print_results(title, folder1, set1, folder2, set2):
	print
	print title + ":"
	print " Total {0}: {1}".format(", ".join( set(folder1.split("/")).difference(set(folder2.split("/")))), len(set1))
	print " Total {0}: {1}".format(", ".join( set(folder2.split("/")).difference(set(folder1.split("/")))), len(set2))
	print " U:", len(set1 | set2)
	print " I:", len(set1 & set2)
	print " {0}-only: {1}".format(", ".join( set(folder1.split("/")).difference(set(folder2.split("/")))), len(set1 - set2))
	print " {0}-only: {1}".format(", ".join( set(folder2.split("/")).difference(set(folder1.split("/")))), len(set2 - set1))
	#print set2 - set1

def main(options):
	# get the params
	project_folder1 = os.path.realpath( os.path.expanduser(args.project1) )
	project_folder2 = os.path.realpath( os.path.expanduser(args.project2) )
	
	coverage1 = storage.read_object(os.path.join(project_folder1, "coverage.pik"))
	coverage2 = storage.read_object(os.path.join(project_folder2, "coverage.pik"))

	# sanity check we're looking at the same project
	totalLines1 = coverage1.getTotalLines()
	totalLines2 = coverage2.getTotalLines()
	assert(totalLines1 == totalLines2)
	
	lines1 = coverage1.getCoveredLines()
	lines2 = coverage2.getCoveredLines()

	bb1 = coverage1.getCoveredBBs()
	bb2 = coverage2.getCoveredBBs()

	edges1 = coverage1.getCoveredEdges()
	edges2 = coverage2.getCoveredEdges()

	print_results("Edges", project_folder1, edges1, project_folder2, edges2)
	print_results("BBs", project_folder1, bb1, project_folder2, bb2)
	print_results("Lines", project_folder1, lines1, project_folder2, lines2)

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