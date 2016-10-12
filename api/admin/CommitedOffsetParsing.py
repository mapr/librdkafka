#!/usr/bin/python
import json
import re
import sys

file_name = sys.argv[1]
with open(file_name) as data_file:
  parsed_json = json.load(data_file)
data_file.close

print(parsed_json["data"][0]['committedoffset'])

