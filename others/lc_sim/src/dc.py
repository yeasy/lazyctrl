#coding:utf-8
#!/usr/bin/python

'''
The data center module for LazyCtrl-Sim project.
called by sim.py
@version: 1.0
@author: U{Baohua Yang<mailto:yangbaohua@gmail.com>}
@created: Oct 16, 2011
@last update: Oct 18, 2011
@see: U{<https://github.com/yeasy/lazyctrl>}
'''
import sys, random, math
from vm import VM
from switch import CoreSwitch, AggSwitch, EdgeSwitch
from group import Group
from statCollector import StatCollector
from defs import DEFALUT_GROUP_ID

class DC:
    """ The data center class. """
    def __init__(self, topo=()):
        """ Init.
        @topo: Topology
        """
        self.topo = topo
        self.num_switch = 0
        self.num_vm = 0
        self.swDic = {} #All switches
        self.coreSWList = [] #Core switches
        self.aggSWList = [] #Agg switches
        self.edgeSWList = [] #Edge switches
        self.vmDic = {} #All vms
        self.num_group = 0
        self.groupDic = {} #All groups: {gid:group}
        self.affinityMatrix={}
        self.avg_focus=0.0
        self.avg_h=0.0
        if topo: #if given a topo, then initialize it
            for swid in topo.coreSWList:
                self.addSwitch(CoreSwitch(swid=swid, type='CORE', num_port=topo.num_port))
            for swid in topo.aggSWList:
                self.addSwitch(AggSwitch(swid=swid, type='AGG', num_port=topo.num_port))
            for swid in topo.edgeSWList:
                self.addSwitch(EdgeSwitch(swid=swid, type='EDGE', num_port=topo.num_port, gid=DEFALUT_GROUP_ID)) #default group
        self.stat = StatCollector()

    def getSW(self, id):
        """ Get the switch by given id.
        @param id: Id of the switch to get
        @return: Switch if exist, otherwise None
        """
        if id in self.swDic:
            return self.swDic[id]
        else:
            return None

    def readinEdgeSWInfo(self, switchfile):
        """ Read in the edge switch and group info from file.
        @switchfile: The switch-group file, line as 'sw_1 group_1...'
        """
        print "read in switch-group mapping info from file %s" % switchfile
        with open(switchfile, 'r') as f:
            try:
                swid = 1
                for ln in f:                
                    gid = int(ln)
                    if swid in self.edgeSWList: #already exist
                        self.updateEdgeSWInfo(swid, gid)
                    else:
                        self.addSwitch(EdgeSwitch(swid, gid))#A new switch
                    swid += 1
            except:
                ex = sys.exc_info()[2].tb_frame.f_back
                print "[file %s, line %s, module %s]: Error when readin %s, " \
                % (__file__, ex.f_lineno, __name__, switchfile)
            finally:
                f.close()
        
    def genGroupFromTraffic(self, trafficfile):
        """ Generate the group info for the edges from traffi files
        Valid groups are [1,2,3,...]
        """
        pass

    def genGroupRandom(self, num_group=4):
        """ Generate the edge switch and group info randomly.
        Valid groups are [1,2,3,...]
        """
        for swid in self.swDic:
            if self.swDic[swid].type == 'EDGE':
                self.updateEdgeSWInfo(swid, random.randint(1, num_group))
                    
    def updateEdgeSWInfo(self, swid, gid):
        """ Update the edge switch and group info randomly
        @param swid: ID of the switch to update
        @param gid: new group id info
        """
        if swid not in self.swDic:
            return
        sw = self.swDic[swid]
        if sw.type != 'EDGE':
            return
        if sw.gid in self.groupDic:
            self.groupDic[sw.gid].removeSwitch(sw)
        if gid not in self.groupDic: #create a new group
            self.addGroup(Group(gid))
        self.groupDic[gid].addSwitch(sw)
        self.swDic[swid].setGid(gid)

    def readinVMInfo(self, vmfile):
        """ Read vm and switch mapping info from file.
        @vmfile: The vm-switch file
            every line is formatted as 'vm1 switch1'
        """
        print "read in vm-switch info from file %s" % vmfile
        with open(vmfile, 'r') as f:
            try:
                for ln in f:               
                    vmid, swid = ln.split()
                    vmid, swid = int(vmid), int(swid)   
                    vm = VM(vmid, swid) #A new VM
                    self.addVM(vm)
                    if swid in self.swDic:
                        self.swDic[swid].addVM(vm)
                    else:
                        raise Exception("No this switch, vmid = %d" % swid)
            except:
                ex = sys.exc_info()[2].tb_frame.f_back
                print "[file %s, line %s, module %s]: Error when readin %s, " \
                % (__file__, ex.f_lineno, __name__, vmfile)
            finally:
                f.close()

    def addGroup(self, group):
        """ Add a new group into the data center's groupDic.
        @group: New added group
        """
        self.groupDic[group.id] = group
        self.num_group += 1
        
    def addSwitch(self, sw):
        """ Add a new switch into the data center's swDic.
        For edgeswitch, should also add the sw into the groupDic
        @param sw: New added switch
        """
        self.swDic[sw.getid()] = sw
        self.num_switch += 1
        if sw.type == "CORE":
            self.coreSWList.append(sw.getid())
        elif sw.type == "AGG":
            self.aggSWList.append(sw.getid())
        elif sw.type == "EDGE":
            self.edgeSWList.append(sw.getid())
            if sw.gid != DEFALUT_GROUP_ID: #update group
                if sw.gid not in self.groupDic: #if new, then create a new group
                    self.addGroup(Group(sw.gid))
                self.groupDic[sw.gid].addSwitch(sw) #add the switch into the group
            if sw.num_vm > 0: #update vmlist
                self.vmDic.update(sw.vmDic)
                self.num_vm += sw.num_vm
        else:
            print "Add error switch, type=%s" % sw.type
            
    def addVM(self, vm):
        """ Add a new vm into the data center's vmDic.
        @vm: New added vm
        """
        self.vmDic[vm.vmid] = vm
        self.num_vm += 1
        
    def testTraffic(self, trafficfile):
        """ Run the trace to stat.
        @trafficfile:For agged trace file, every line is as 'host1 host2 flow oct...'
        @trafficfile:For trace file, every line is as 'host1 host2 oct...'
        """
        #f_in_group = open("in_group.txt", "w")
        #f_out_group = open("out_group.txt", "w")
        print "Test the flow record from file %s" % trafficfile
        i = 0
        flow = 1
        with open(trafficfile, 'r') as f:
            try:
                for ln in f:
                    src, dst, flow, oct = ln.split()
                    src, dst, flow, oct = int(src), int(dst), int(flow), float(oct)
                    #src, dst, oct = ln.split()
                    #src, dst, oct = int(src), int(dst), float(oct)
                    if src in self.vmDic and dst in self.vmDic:
                        self.sendVMTraffic(self.vmDic[src], self.vmDic[dst], flow, oct)
                        #if self.sendVMTraffic(self.vmDic[src], self.vmDic[dst], flow, oct):
                            #f_in_group.write("%d %d %d\n" % (src, dst, flow))
                        #else:
                            #f_out_group.write("%d %d %d\n" % (src, dst, flow))
                    i += 1
                    if i % 1000000 == 0:
                        print i
            except:
                ex = sys.exc_info()[2].tb_frame.f_back
                print "[file %s, line %s, module %s]: Error when readin %s, " \
                % (__file__, ex.f_lineno, __name__, trafficfile)
                print "read and process %d lines" % (i)
            finally:
                f.close()              
        #f_in_group.close()
        #f_out_group.close()

    def sendVMTraffic(self, src, dst, flow, oct):
        """ Send flow from vm src to vm dst.
        @src: Source vm
        @dst: Dst vm
        @flow: number of flows to send
        @oct: Bytes of the traffic to send
        @return: True if in-group flow
        """
        is_in_group = True #denote if in the same group
        stat = self.stat
        if src.getSWid() == dst.getSWid(): #check arp table, found at the same switch
            stat.addQueryArp(flow)
            stat.addFlowLocal(flow)
            stat.addTrafficLocal(oct)
            return True
        src_sw, dst_sw = self.getSW(src.getSWid()), self.getSW(dst.getSWid())
        src_group_id, dst_group_id = src_sw.getGid(), dst_sw.getGid()
        if src_group_id not in self.groupDic:
            print "src_group_id = %d, not here" % src_group_id
            return
        if src_group_id not in self.affinityMatrix:
            self.affinityMatrix[src_group_id] = {}
            for i in range(self.num_group):
                self.affinityMatrix[src_group_id][i] = 0
        if dst_group_id not in self.affinityMatrix[src_group_id]:
            self.affinityMatrix[src_group_id][dst_group_id] = flow
        else:
            self.affinityMatrix[src_group_id][dst_group_id] += flow
        src_group = self.groupDic[src_group_id]
        #stat the flow 
        if src_group_id == dst_group_id:
            stat.addFlowWithin(flow)
            stat.addTrafficWithin(oct)
            is_in_group = True
        else:
            stat.addFlowCross(flow)
            stat.addTrafficCross(oct)
            is_in_group = False
        #stat the queries 
        hit_sw = []
        for swid in src_group.swDic:#test each sw in the same group
            if swid != src_sw.getid(): #one sw says the vm is there
                if self.swDic[swid].hasVMbyBF(dst):
                    hit_sw.append(swid)
        if len(hit_sw) == 0: #no hit, means in other group
            stat.addTN(flow)
            stat.addQueryControlCurrent(flow)
        else: #at least one hit
            #stat.addQueryArp(len(hit_sw)) #each sw check its arp table
            FOUND = False
            for swid in hit_sw: #test the sws hitted in bfs
                if self.getSW(swid).hasVM(dst): #actually there
                    FOUND = True
                    break
            if FOUND: #find in one switch
                stat.addTP(flow)
                stat.addQueryControlCurrent(flow * (len(hit_sw) - 1)) #all others are false
            else:
                stat.addFP(flow) #check all hit switches, find no
        stat.addQueryControlOrigin(flow)
        return is_in_group

    def testShuffleTraffic(self, num_test=1000, control_threshold=0.1):
        """ Test with auto-generated traffic.
        @param num_test: Number of the random test
        @param control_threshold: How many flows are cross-group flows
        """
        traffic, count = 1, 0
        while count < num_test:
            if random.uniform(0, 1.0) < control_threshold: #should accross group
                while True:
                    src_vm = self.vmDic[random.choice(self.vmDic.keys())]
                    dst_vm = self.vmDic[random.choice(self.vmDic.keys())]
                    src_sw, dst_sw = self.swDic[src_vm.getSWid()], self.swDic[dst_vm.getSWid()]
                    if src_vm != dst_vm and src_sw.getGid() != dst_sw.getGid():
                        break
            else: #should within group
                while True:
                    src_vm = self.vmDic[random.choice(self.vmDic.keys())]
                    dst_vm = self.vmDic[random.choice(self.vmDic.keys())]
                    src_sw, dst_sw = self.swDic[src_vm.getSWid()], self.swDic[dst_vm.getSWid()]
                    if src_vm != dst_vm and src_sw.getGid() == dst_sw.getGid():
                        break
            self.sendVMTraffic(src_vm, dst_vm, traffic)
            count += 1

    def getAvgH(self):
        H = []
        f = self.affinityMatrix
        for i in range(self.num_group):
            if i in f:
                Ti = sum(f[i].values())
                p = sum([float(f[i][j])/Ti for j in range(self.num_group)])
                H.append(p)
            else:
                H.append(0.0)
        return sum(H)/(float(len(H))*math.log(len(H),2))
        
    def showInfo(self):
        """ Print basic information.
        """
        print ">>[DC Info]:"
        print "#switches = %u, core = %u, agg = %u, edge = %u"\
                % (self.num_switch, len(self.coreSWList), len(self.aggSWList), len(self.edgeSWList))
        print "#hosts = %u" % self.num_vm,
        print "#groups = %u" %(self.num_group)
        #for g in self.groupDic.values(): #print info of each group
        #    g.showInfo()
        focus = []
        f = self.affinityMatrix
        for i in range(self.num_group):
            if i in f and sum(f[i].values())!=0:
                focus.append(float(f[i][i])/sum(f[i].values()))
            else:
                print "No flow in group %u, set its focus to 1.0" %i
                focus.append(1.0)
        print "average_focus=",float(sum(focus))/len(focus)
        print "average_H=",self.getAvgH()
            
    def showStat(self):
        """ Print statistical information.
        """
        self.stat.showStat()
