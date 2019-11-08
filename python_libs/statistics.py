import os, sys, inspect
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
parentdir = os.path.dirname(currentdir)
sys.path.insert(0, parentdir)

import numpy as np
import scipy.stats as stats


"""

list of tests:
- z-test: compares two distribution, assumes normal distribution but for small sets
- t-test: same as above but with lager samples
- Mann Whitney U test: compares two distribution, , no assumption on being normal distribution
- Kolmorov-Smirnov: compare distrib.. what assumption on distrib?
- Fisher's exact test
- Chi-squared test
- Shapiro-Wilk/D'agostino K-squared/Lilliefors/Shapiro Francia: test if it's noral distribution
- Cramer Von Mises: check if sample comes from a distribution

"""

def mean_std_confidence_interval(data, confidence=0.95):
	a = 1.0 * np.array(data)
	n = len(a)
	m, se = np.mean(a), stats.sem(a)
	std = np.std(a)
	h = se * stats.t.ppf((1 + confidence) / 2., n-1)
	return m, std, m-h, m+h

# def twoSampZ(X1, X2, mudiff, sd1, sd2, n1, n2):
# 	from numpy import sqrt, abs, round
# 	from scipy.stats import norm
# 	pooledSE = sqrt(sd1**2/n1 + sd2**2/n2)
# 	z = ((X1 - X2) - mudiff)/pooledSE
# 	#pval = 2*(1 - norm.cdf(abs(z)))
# 	pval = 2*(1 - stats.t.cdf(abs(z)))
# 	return round(z, 3), round(pval, 4)
#z, p = twoSampZ(m1, m2, 0, std1, std2, 10, 10)

def median(data):
	return np.median(data)

def scoreatpercentile(data, value):
	return stats.scoreatpercentile(data, value)

def median_absolute_deviation(data):
	return np.mean(np.absolute(data - np.mean(data))) 

def mean(data):
	a = 1.0 * np.array(data)
	return np.mean(a)

def ttest(data1, data2):
	# https://towardsdatascience.com/inferential-statistics-series-t-test-using-numpy-2718f8f9bf2f
	return stats.ttest_ind(data1, data2)

# https://machinelearningmastery.com/nonparametric-statistical-significance-tests-in-python/
# the tests below are called 'nonparametric', as they assume no particular distribution for the underlying data


# Mann-Whitney U Test: for comparing independent data samples: the nonparametric version of the Student t-test.
def mannwhitneyu(data1, data2):
	return stats.mannwhitneyu(data1, data2)

# Wilcoxon Signed-Rank Test: for comparing paired data samples: the nonparametric version of the paired Student t-test.
def wilcoxon(data1, data2):
	return stats.wilcoxon(data1, data2)

# Kruskal-Wallis H Test: for comparing more than two data samples: the nonparametric version of the ANOVA and repeated measures ANOVA tests.
def kruskal(data1, data2):
	return stats.kruskal(data1, data2)

def friedmanchisquare(*datas):
	return stats.friedmanchisquare(datas)