import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)

from . import c2s

class Coverage:

	def __init__(self):
		pass

	def __str__(self):
		return "Coverage"

	#  this is with a dedicated BB shared buffer
	def __extract_bbs(self, filename, bb_seen, assertion, display_progress=False):
		bbSet = set([])
		with open(filename, "rb") as file:
			contents = file.read()
			i = 0
			for byte_n in range(len(contents)):
				c = ord(contents[byte_n])
				assert(c <= 255)
				assertion(c)
				for bit in range(8):
					seen = (c & (1 << bit)) != 0
					if bb_seen(seen):
						bbid = byte_n*8 + bit
						# print byte_n, bit
						bbSet.add(bbid)
					i += 1
					if display_progress and i % 10000 == 0:
						sys.stdout.write(".")
						sys.stdout.flush()

		return bbSet
			
	def __extract_lines(self, bbSet, c2s_dict):
		lineSet = set([])
		c = 0
		nc = 0
		for bbid in bbSet:
			# sanity check we have seens the cov ID at compilation
			assert ( bbid in c2s_dict.keys() )
			for line in c2s_dict[bbid]:
				# we may not have the src info, because the BB does not correpsond to an actual line of source code
				if len(line):
					lineSet.add(line)
		return lineSet

	def __extract_edges(self, edgefile, edge_seen_afl):
		edgeSet = set([])
		with open(edgefile, "rb") as file:
			contents = file.read()
			i = 0
			for c in contents:
				if edge_seen_afl(c):
					edgeSet.add(i)
				i += 1
		return edgeSet

	
	def extract_coverage(self, c2sfile, bbfile, edgefile):
		def edge_seen_afl(c): return 0 if ord(c) == 0xff else 1
		def bb_seen_afl(c): return 0 if c == 0 else 1
		def assertion_afl(c): pass

		c2sDict = c2s.read(c2sfile)
		bbSet = self.__extract_bbs(bbfile, bb_seen_afl, assertion_afl)
		lineSet = self.__extract_lines(bbSet, c2sDict)
		edgeSet = self.__extract_edges(edgefile, edge_seen_afl);

		co = CoverageObject()
		co.setData(set([line for bbid in c2sDict for line in c2sDict[bbid]]), bbSet, lineSet, edgeSet)
		return co


def extract_coverage(c2sfile, bbfile, edgefile):
	cov = Coverage()
	return cov.extract_coverage(c2sfile, bbfile, edgefile)
	
	
class CoverageObject:

	def __init__(self):
		self._totalLines = None
		self._coveredLines = None
		self._coveredBBs = None
		self._coveredEdges = None

	def __str__(self):
		return "CoverageObject"

	def setData(self, totalLineSet, bbSet, lineSet, edgeSet):
		self._totalLines = totalLineSet
		self._coveredLines = lineSet
		self._coveredBBs = bbSet
		self._coveredEdges = edgeSet

	def getTotalLines(self):
		assert(self._totalLines != None)
		return self._totalLines

	def getCoveredLines(self):
		assert(self._coveredLines != None)
		return self._coveredLines

	def getCoveredBBs(self):
		assert(self._coveredBBs != None)
		return self._coveredBBs

	def getCoveredEdges(self):
		assert(self._coveredEdges != None)
		return self._coveredEdges

# this is for no-collision coverage
# def read_edges(filename, edge_seen, assertion, c2s_dict):
# 	srcSet = set([])
# 	with open(filename, "rb") as file:
# 		contents = file.read()
# 		i = 0
# 		edges_n = 0
# 		for c in contents:
# 			assertion(c)
# 			if edge_seen(c):
# 				edges_n += 1
# 				# want to make sure we have seens the cov ID at compilation
# 				assert ( i in c2s_dict.keys() )
# 				for src in c2s_dict[i]:
# 					# we may not have the src info
# 					if len(src):
# 						srcSet.add(src)
# 			i += 1
# 			if i % 10000 == 0:
# 				sys.stdout.write(".")
# 				sys.stdout.flush()

# 	return srcSet, edges_n