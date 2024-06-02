import random
import os
import re

m = {}
with open("input.txt") as src, open("temp.txt", "w") as dest:
    for line in src:
        if line[0] == '#':
            continue
        res = re.findall(r'\(.*?\)', line)[0]
        if(res not in m):
            m[res] = []
        m[res].append(line)
    for key in m:
        seen = False
        for s in m[key]:
            if '%' in s:
                seen = True
                break
        if seen:
            m[key] = []
    for key in m:
        for s in m[key]:
            dest.write(s)
    
    
os.rename("temp.txt", "output.txt")