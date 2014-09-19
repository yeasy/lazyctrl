#!/usr/bin/python
#coding:utf-8
'''The main simulation script for the LazyCtrl-Sim project.
    This prog will simulate an lc-enable datacenter (all sws are grouped). 
    With given traffic records, it will collect the in/out group stats, etc.
    Related modules: dc (vm, switch, group, bf, statCollector, util), topo.
    This prog suppots two modes: manual and auto.
    In manual mode: should given host-sw mapping, sw-group mapping, and the traffic record 
@version: 1.0
@author: U{Baohua Yang<mailto:yangbaohua@gmail.com>}
@created: Oct 12, 2011
@last update: Nov 18, 2011
@see: U{<https://github.com/yeasy/lazyctrl>}
@TODO: nothing
'''
import os, profile,glob
from dc import DC
from topo import Random, Flat, FatTree
from optparse import OptionParser, OptionGroup
from timeit import Timer

PROG_CUT = './mincut'

#def testManu(host_file='host_sw_map.txt',sw_file='sw_group_map.txt',trace_file='out_group.txt'):
def testManu(host_file='host_sw_map.txt', sw_file='metis_input.txt.part.10', trace_file='flow_anon_2008-01-01.agg.txt'):
    """Read host,sw,trace from out files, and run the test.
    @param host_file: File that stores the host-sw mapping information.
        every line with format: host_id sw_id
    @param sw_file: File that stores the sw-group mapping information
    @param trace_file: File that stores the trace record information
    """
    if not (os.path.isfile(host_file) and os.path.isfile(sw_file)):
        print "[ERROR] Invalid file name, host_sw_map=%s, sw_group_map=%s." %(host_file,sw_file)
        return
    dc = DC()
    print "readin sw-group mapping information."
    dc.readinEdgeSWInfo(sw_file)
    print "readin host-sw mapping information."
    dc.readinVMInfo(host_file)
    #dc.showInfo()
    print "readin trace file."
    if os.path.isfile(trace_file):
        dc.testTraffic(trace_file)
    else:
        print "%s, No traffic file is given for testing." %trace_file
    dc.showInfo()
    dc.showStat()
    
def testAuto(num_sw=16, num_port=48, num_group=4, num_tries=10000, control_threshold=0.9):
    """Run an automatic test with given parameters.
    Generate group randomly, and send traffic randomly with control_threshold
    @param num_sw: Number of switches in the datacenter
    @param num_port: Number of ports of each switch
    @param num_group: Number of groups in the datacenter
    @param num_tries: Number of random tests
    @param control_threshold: How many percent flows are cross-group
    """
    dc = DC(Flat(num_sw=num_sw, num_port=num_port))
    dc.genGroupRandom(num_group=num_group)
    dc.testShuffleTraffic(num_tries, control_threshold)
    dc.showInfo()
    dc.showStat()

def doParser():
    """
    Parse the parameters given to the prog.
    @return: opt,args
    """
    parser = OptionParser(usage="usage: %prog [-v HOST_FILE][-s SWITCH_FILE][-t TRACE_FILE]|[-a][-n NUMBER_SWITCH][-N NUMBER_GROUP][-r NUMBER_TEST]", version="%prog 1.0")
    group_auto = OptionGroup(parser, "Options for automatic mode")
    group_mannu = OptionGroup(parser, "Options for mannual mode")
    group_auto.add_option("-a", "--auto_mode",
            action="store_true",
            dest="auto_mode", default=False,
            help="turn on the automatic test mode", metavar="AUTO_MODE")
    group_auto.add_option("-n", "--num_sw",
            action="store", type="int",
            dest="num_sw", default=0,
            help="number of switches. Only work in auto mode.", metavar="NUM_SWITCH")
    group_auto.add_option("-N", "--num_group",
            action="store", type="int",
            dest="num_group", default=0,
            help="number of groups. Only work in auto mode.", metavar="NUM_GROUP")
    group_auto.add_option("-r", "--num_tests",
            action="store", type="int",
            dest="num_test", default=0,
            help="number of random tests. Only work in auto mode.", metavar="NUM_TEST")
    group_mannu.add_option("-v", "--host_file",
            action="store", type="string",
            dest="host_file", default="",
            help="read the host and corresponding switch info from FILE", metavar="HOST_FILE")
    group_mannu.add_option("-s", "--sw_file",
            action="store", type="string",
            dest="sw_file", default="",
            help="read the switch and corresponding group info from FILE", metavar="SWITCH_FILE")
    group_mannu.add_option("-t", "--trace_file",
            action="store", type="string",
            dest="trace_file", default="",
            help="read the trace record from FILE", metavar="TRACE_FILE")
    parser.add_option_group(group_auto)
    parser.add_option_group(group_mannu)
    return parser.parse_args()
    
if __name__ == "__main__":
    """
    The main module of HC simulator.
    Basic usage: 
        ./sim.py -a [-n num_sw][-N num_group][-r random_test] #auto mode
        ./sim.py -v host_sw_map_file -s sw_group_map_file -t trace_file #manual mode
    """
    #profile.run("testManu()")
    #exit()
    try:
        psyco.full()
    except:
        pass
    #t = Timer("testManu()", "from __main__ import testManu")
    #print "running time =", t.timeit(1)
    #exit()
    #print os.execvp(PROG_CUT,(PROG_CUT,)+tuple(['sw_.txt']))
    (opt, args) = doParser()
    if opt.auto_mode and opt.num_sw > 0 and opt.num_group > 0 and opt.num_test > 0: #automatic test mode
        testAuto(opt.num_sw, opt.num_group, opt.num_test)
    elif not opt.auto_mode and opt.host_file and opt.sw_file and opt.trace_file: #manual mode
        if os.path.isdir(opt.trace_file): #given a directory
            for fn in glob.glob(opt.trace_file + os.sep + '*.txt'):
                testManu(opt.host_file, opt.sw_file, fn)
        else:
            testManu(opt.host_file, opt.sw_file, opt.trace_file)
    else:
        print "Invalid parameter number, try -h"
