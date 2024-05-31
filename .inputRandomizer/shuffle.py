import random

def return_only_ascii(str):
    return ''.join([x for x in str if x.isascii()])

#n_start=6282
#n_end=85921
with open("input.txt", encoding="utf8") as f:
    lines = f.readlines()
#lines=[return_only_ascii(x) for x in lines]
#tmp=lines[n_start:n_end]
random.shuffle(lines)
#lines[n_start:n_end]=tmp
with open("outfile.txt", "w", encoding="utf8") as f:
    f.writelines(lines)