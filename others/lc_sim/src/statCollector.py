#coding:utf-8
#!/usr/bin/python

'''The statistical result collector module for the HC project.
@version: 1.0
@author: U{Baohua Yang<mailto:yangbaohua@gmail.com>}
@created: Oct 16, 2011
@last update: Nov 18, 2011
@see: U{<https://github.com/yeasy/lazyctrl>}
'''
from defs import DEFALUT_GROUP_ID

class StatCollector:
    """ The statistical information collector class. """
    def __init__(self, gid=DEFALUT_GROUP_ID):
        """ Init.
        """
        self.gid = gid
        self.query_arp = 0 # number of local arp queries
        self.query_control_origin = 0 # number of total queries of each flow
        self.query_control_current = 0 # number of queries to controller
        self.flow_local = 0 #flows within local switch
        self.flow_within_group = 0 #flows within one group
        self.flow_cross_group = 0 #flows cross different groups
        self.byte_local = 0 # bytes within local switch
        self.byte_within_group = 0 # bytes within the same group
        self.byte_cross_group = 0 # bytes between different groups
        self.tp = 0 # number of true positive queries
        self.tn = 0 # number of true negative queries
        self.fp = 0 # number of false positive queries

    def addQueryArp(self, num=1):
        """ Add local arp query.
        @param num: Number to add
        """
        self.query_arp += num

    def addQueryControlOrigin(self, num=1):
        """ Add query number.
        @param num: Number to add
        """
        self.query_control_origin += num

    def addQueryControlCurrent(self, num=1):
        """ Add query number to controller.
        @param num: Number to add
        """
        self.query_control_current += num

    def addFlowLocal(self, flow=1):
        """ Add flows at the same switch.
        @param flow: Flows to add
        """
        self.flow_local += flow

    def addFlowWithin(self, flow=1):
        """ Add flows to other switch of the same groups.
        @param flow: Flows to add
        """
        self.flow_within_group += flow

    def addFlowCross(self, flow=1):
        """ Add flows to other groups.
        @param flow: Flows to add
        """
        self.flow_cross_group += flow

    def addTrafficLocal(self, byte):
        """ Add a traffic at the same switch
        @param byte: Byte to add
        """
        self.byte_local += byte

    def addTrafficCross(self, byte):
        """ Add a traffic crossing groups.
        @param byte: Byte to add
        """
        self.byte_cross_group += byte

    def addTrafficWithin(self, byte):
        """ Add a traffic within the group.
        @param byte: Byte to add
        """
        self.byte_within_group += byte

    def addTP(self, num=1):
        """ Add a True Positive.
        """
        self.tp += num
        
    def addTN(self, num=1):
        """ Add a True Negative.
        """
        self.tn += num

    def addFP(self, num=1):
        """ Add a False Positive.
        """
        self.fp += num

    def showStat(self):
        """ Print the statistical information.
        """
        print ">>[Stat Information]:"
        if self.gid != DEFALUT_GROUP_ID:
            print "Gid = %u" % self.gid
        print "[Queries] Arp = %u, Original_to_controller= %u, Current_to_controller = %u" % (self.query_arp, self.query_control_origin, self.query_control_current)
        print "TP = %u, TN = %u, FP = %u" % (self.tp, self.tn, self.fp)
        print "[Flow] local_switch = %u, within the group = %u,across groups = %u" % (self.flow_local, self.flow_within_group, self.flow_cross_group)
        print "[Traffic] local_switch = %u byte, within the group = %u byte,across groups = %u byte" % (self.byte_local, self.byte_within_group, self.byte_cross_group)

    def resetStat(self):
        """ reset.
        """
        self.query_arp = 0 # number of local arp queries
        self.query_control_origin = 0 # number of total queries of each flow
        self.query_control_current = 0 # number of queries to controller
        self.flow_local = 0 #flows within local switch
        self.flow_within_group = 0 #flows within one group
        self.flow_cross_group = 0 #flows cross different groups
        self.byte_local = 0 # bytes within local switch
        self.byte_within_group = 0 # bytes within the same group
        self.byte_cross_group = 0 # bytes between different groups
        self.tp = 0 # number of true positive queries
        self.tn = 0 # number of true negative queries
        self.fp = 0 # number of false positive queries

if __name__ == "__main__":
    stat = StatCollector()
    stat.showStat()
