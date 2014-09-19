#!/usr/bin/python
#coding:utf-8
'''
Process the output partition file by metis.
@version: 1.0
@author: U{Baohua Yang<mailto:yangbaohua@gmail.com>}
@created: Dec 22, 2012
@last update: Dec 25, 2012
@see: U{<https://github.com/yeasy/lazyctrl>}
@TODO: nothing
'''
import os
import glob, fileinput
from optparse import OptionParser

def processMetisPartition(fn_in, fn_out):
    """
    @param fn_in: Name of the input orginal partition file: *.part.#num
    @param fn_out: Name of the output file
        format: file_name num_group max_group_size
    """
    if not fn_in or not fn_out:
        return
    f_in = open(fn_in, "r")
    f_out = open(fn_out, "a")
    i, gid = 1, 0
    group_dic={};
    try:
        for line in fileinput.input([fn_in]):
            gid = int(line[0])
            if gid not in group_dic:
                group_dic[gid] = [i]
            else:
                group_dic[gid].append(i)
            i += 1
    finally:
        print "%s %d %d" %(fn_in,len(group_dic.values()),max([len(i) for i in group_dic.values()]))
        f_out.write("%s %d %d\n" %(fn_in,len(group_dic.values()),max([len(i) for i in group_dic.values()])))
        f_in.close()
        f_out.close()
    
    
if __name__ == "__main__":
    parser = OptionParser(usage="Usage: %prog [-i FILE][-o FILE]", version="%prog 1.0")
    parser.add_option("-i", "--input_file",
            action="store", type="string",
            dest="input", default="vm_pair_flow.txt",
            help="read trace data from FILE", metavar="FILE")
    parser.add_option("-o", "--output_file",
            action="store", type="string",
            dest="output", default="",
            help="write result into FILE", metavar="FILE")
    (opt, args) = parser.parse_args()
    if not opt.input or not opt.output:
        print "Invalid parameter number, try -h"
        print "Usage: %prog [-i FILE][-o FILE]"
        exit()
    if os.path.isdir(opt.input): #given a directory
        for fn_in in glob.glob(opt.input + os.sep + '*.*'):
            processMetisPartition(fn_in, opt.output)
    else:
        processMetisPartition(opt.input, opt.output)
