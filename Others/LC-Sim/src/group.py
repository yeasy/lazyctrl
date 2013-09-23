#!/usr/bin/python
#coding:utf-8

'''The Group module for the HC project.
@version: 1.0
@author: U{Baohua Yang<mailto:yangbaohua@gmail.com>}
@created: Oct 16, 2011
@last update: Nov 18, 2011
@see: U{<https://github.com/yeasy/lazyctrl>}
'''
from statCollector import StatCollector
from defs import DEFALUT_GROUP_ID

class Group:
    """ The data center class. """
    def __init__(self, id=DEFALUT_GROUP_ID):
        """ Init.
        @param id: Group id
        """
        self.num_switch = 0
        self.id = id
        self.swDic = {} #The switches that belong to the group
        self.bfDic = {} #Dic of the switches bf. {switch_id: bf}
        self.stat = StatCollector()
                
    def addSwitch(self, sw):
        """ Add a new switch into this group.
        @param sw: Switch to add in
        """
        self.swDic[sw.getid()] = sw
        self.bfDic[sw.getid()] = sw.bf
        self.num_switch += 1
    
    def removeSwitch(self, sw):
        """ Remove a sw from the group.
        @param sw: Switch to remove
        """
        #TODO: Delete key from bf?
        #self.bf.update([sw])
        self.swDic.pop(sw.getid())
        self.bfDic.pop(sw.getid())
        self.num_switch -= 1
    
    def hasSWbyMe(self, sw):
        """ Check local sw list to see if the sw in group.
        @param sw: Switch to test
        @return: True or False
        """
        if sw.getid() in self.swDic:
            return True
        else:
            return False
    
    def showInfo(self):
        """ Print basic information.
        """
        print ">>Group[%u]:" % self.id,
        print "%u switches:" % self.num_switch,
        print self.swDic.keys()
        
    def showStat(self):
        """ Print the statistical information.
        """
        self.stat.show()

if __name__ == "__main__":
    group = Group()
    group.showInfo()
