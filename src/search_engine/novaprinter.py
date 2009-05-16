#VERSION: 1.31

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright notice,
#      this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of the author nor the names of its contributors may be
#      used to endorse or promote products derived from this software without
#      specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

import sys

# Force UTF-8 printing
from ctypes import pythonapi, py_object, c_char_p
PyFile_SetEncoding = pythonapi.PyFile_SetEncoding
PyFile_SetEncoding.argtypes = (py_object, c_char_p)
PyFile_SetEncoding(sys.stdout, "UTF-8")

def prettyPrinter(dictionary):
	# Convert everything to unicode for safe printing
	for key,value in dictionary.items():
		if isinstance(dictionary[key], str):
			dictionary[key] = unicode(dictionary[key], 'utf-8')
	dictionary['size'] = anySizeToBytes(dictionary['size'])
	print u"%s|%s|%s|%s|%s|%s"%(dictionary['link'],dictionary['name'],dictionary['size'],dictionary['seeds'],dictionary['leech'],dictionary['engine_url'])

def anySizeToBytes(size_string):
	"""
	Convert a string like '1 KB' to '1024' (bytes)
	"""
	# separate integer from unit
	try:
		size, unit = size_string.split()
	except:
		try:
			size = size_string.strip()
			unit = ''.join([c for c in size if c.isalpha()])
			size = size[:-len(unit)]
		except:
			return -1
	if len(size) == 0:
		return -1
	size = float(size)
	short_unit = unit.upper()[0]

	# convert
	units_dict = { 'T': 40, 'G': 30, 'M': 20, 'K': 10 }
	if units_dict.has_key( short_unit ):
		size = size * 2**units_dict[short_unit]
	return int(size)
