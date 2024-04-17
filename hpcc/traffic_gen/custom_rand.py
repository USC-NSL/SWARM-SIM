import random

class CustomRand:
	"""
	Provides an interface for sampling a piecewise linear CDF
	"""

	@classmethod
	def test_cdf(cls, cdf):
		"""
		Tests the CDF to see if it makes sense
		"""

		if cdf[0][1] != 0:
			return False
		if cdf[-1][1] != 100:
			return False
		for i in range(1, len(cdf)):
			if cdf[i][1] <= cdf[i-1][1] or cdf[i][0] <= cdf[i-1][0]:
				return False
		return True
	
	def __init__(self, cdf):
		assert self.test_cdf(cdf)
		self.cdf = cdf
	
	def get_avg(self):
		s = 0
		last_x, last_y = self.cdf[0]
		
		for x, y in self.cdf[1:]:
			s += (x + last_x)/2.0 * (y - last_y)
			last_x = x
			last_y = y

		return s/100
	
	def rand(self):
		r = random.random() * 100
		return self.get_value_from_percentile(r)
			
	def get_value_from_percentile(self, y):
		for i in range(1, len(self.cdf)):
			if y <= self.cdf[i][1]:
				x0,y0 = self.cdf[i-1]
				x1,y1 = self.cdf[i]
				return x0 + (x1-x0)/(y1-y0)*(y-y0)
		
		# We should never end up here
		raise ValueError()
