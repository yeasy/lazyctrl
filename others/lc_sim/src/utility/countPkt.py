#!/usr/bin/python

import os,glob,math,fileinput

def doCount(fn):
    if not fn:
        return
    src, dst, octs = 0, 0, 0
    sum = 0
    try:
        for line in fileinput.input([fn]):
            src, dst, octs = line.split()
            src, dst, octs = int(src), int(dst), int(octs)
            sum += math.ceil(float(octs)/1518.0)
    finally:
        print "%s, pkts= %u" %(fn,sum)

if __name__ == "__main__":
    fn_in = "../result/traffic_record_per1hour"
    if os.path.isdir(fn_in): #given a directory
        for fn in glob.glob(fn_in + os.sep + '*.record.txt'):
            doCount(fn)
