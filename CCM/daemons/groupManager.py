#!/usr/bin/python
#coding:utf-8
'''The main groupManager module for the LazyCtrl project.
    This prog will act as the groupmanager in an lc-enable datacenter (all sws are grouped). 
    With given traffic stat records, it will generate an grouping results, etc.
@version: 1.0
@author: U{Baohua Yang<mailto:baohyang@cn.ibm.com>}
@created: Oct 12, 2012
@last update: Aug 10, 2013
@see: U{<https://github.com/yeasy/lazyctrl>}
@TODO: Test more grouping algorithms.
'''
import os, profile,glob,time
import fileinput

PROG_CUT = './gpmetis'

def doGroup(fn_cpu="/tmp/cpus.dat",fn_stat="/tmp/lc_stats.dat", fn_to_group="/tmp/lc_stats.toGroup",num_group=5):
    """
    Read the cpu/stat record collected from each DDCM, format it and do grouping.
    @param fn_stat: Name of the input stat file with each line meaning one flow.
        format: src dst weight
    @param fn_to_group: 
    a head line of "numSw numFlow 001"
    flows...
    """
    if not fn_stat or not fn_to_group:
        return
    print fn_stat
    f_out = open(fn_to_group, "w")
    src, dst, w = 0, 0, 0
    swSet=set()
    flowDic = {} #{(sw1, sw2):flow}
    num_flow = 0
    num_cpu =0
    new_num_group=num_group

    try:
        for line in fileinput.input([fn_stat]):
            num_flow += 1
            src, dst, w = line.split()
            src, dst, w = int(src), int(dst), int(w)
            swSet.add(src)
            swSet.add(dst)
            t = tuple(sorted([src, dst]))
            flowDic[t] = w
        f_out.write("%u %u %s\n" % (len(swSet), num_flow,"001"))
        for k in flowDic:
            f_out.write("%u %u %u\n" % (k[0], k[1], flowDic[k]))
        print " %d records are read, with %d switches [%d,%d], flows=%d"\
                %(num_flow,len(swSet),min(swSet),max(swSet),sum([num_flow for num_flow in flowDic.values()]))
        for line in fileinput.input([fn_cpu]): #some sw are complaining
            num_cpu += 1
        new_num_group = num_group/(1.0+float(num_cpu)/len(swSet)) #other principles can be utilized here
        if new_num_group != num_group:
            os.system("%s -ncuts=4 -ufactor=1.1 -niter=20 %s %u >> group_result.summary" %(PROG_CUT,fn_to_group,new_num_group))
            os.system("cp %s /tmp/group_result.dat" %(fn_to_group+".part."+repr(new_num_group)))
    finally:
        f_out.close()
    return new_num_group


def doPush():
    dcm_list = []
    ddcm_list = []
    for line in fileinput.input(["dcm_ip.list"]):
        dcm_list.append(line)
    for line in fileinput.input(["ddcm_ip.list"]):
        ddcm_list.append(line)
    i = 0
    for line in fileinput.input(["/tmp/group_result.dat"]):
        ip = dcm_list[i]
        gid = int(gid)
        if ip in ddcm_list: #ddcm should push with flag 1
            msg = repr(gid)+" "+"1"
        else:
            msg = repr(gid)+" "+"0"
        os.system("python ./push_agent.py %s %s" %(ip,msg))
        i += 1
    pass
    

    
if __name__ == "__main__":
    """
    The main groupManager module of LazyCtrl.
    """
    #profile.run("doGroup()")
    #exit()
    try:
        psyco.full()
    except:
        pass
    while(True):
        num=2
        num=doGroup(num_group=num)
        doPush()
        time.sleep(20)
