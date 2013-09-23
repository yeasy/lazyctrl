#coding:utf-8
#!/usr/bin/python

'''The switch module for the HC project.
@version: 1.0
@author: U{Baohua Yang<mailto:yangbaohua@gmail.com>}
@created: Oct 16, 2011
@last update: Oct 31, 2011
@see: U{<https://github.com/yeasy/lazyctrl>}
'''
from vm import VM
from bf import BloomFilter, BloomFilter_4k, BloomFilter_128, BloomFilter_256

class Switch:
    """ The Switch class. """
    def __init__(self, swid, type, num_port=0):
        """ Init.
        @param swid: Switch id
        @param type: Switch type
        @param num_port: Number of port of each switch, 
                    suppose each port connects one vm
        """
        self.swid = swid
        self.num_port = num_port
        self.type = type
                    
    def setid(self, swid):
        """ Set a vmid for the switch.
        @vmid: vmid to set
        """
        self.swid = swid
        
    def getid(self):
        """ Get the vmid of the switch. 
        @return: Return the vmid
        """
        return self.swid
    
class CoreSwitch(Switch):
    """ The Switch class. """
    def __init__(self, swid, type="CORE", num_port=0):
        """ Init.
        @param swid: Switch id
        @param type: Switch type
        @param num_port: Number of port of each switch, 
                    suppose each port connects one vm
        """
        Switch.__init__(self, swid, type, num_port)
        
class AggSwitch(Switch):
    """ The Switch class. """
    def __init__(self, swid, type="AGG", num_port=0):
        """ Init.
        @param swid: Switch id
        @param type: Switch type
        @param num_port: Number of port of each switch, 
                    suppose each port connects one vm
        """
        Switch.__init__(self, swid, type, num_port)
    
class EdgeSwitch(Switch):
    def __init__(self, swid, gid, type='EDGE', num_port=0):
        """ Init switch, add vms.
        @param swid: Switch id
        @param gid: Group id
        @param type: Switch type
        @param num_port: Number of port of each switch, 
                    suppose each port connects one vm
        """
        Switch.__init__(self, swid, type, num_port)
        self.gid = gid
        self.num_vm = num_port
        self.vmDic = {}
        self.bf = BloomFilter_256(16) #a bf class
        #self.bf = BloomFilter_4k(10) #a bf class 4 KB*10
        for i in range(num_port):
            vm = VM(vmid=swid * num_port + i, swid=swid)
            self.addVM(vm)
            
    def setGid(self, gid):
        """ Set a gid for the switch.
        @gid: Group vmid to set
        """
        self.gid = gid
        
    def getGid(self):
        """ Get the gid of the switch. 
        @return: Return the group vmid
        """
        return self.gid
    
    def addVM(self, vm):
        """ Attach a new vm into this switch.
        @vm: VM to attach
        """
        self.vmDic[vm.getid()] = vm
        self.bf.update([vm.getid()]) #Add new element.
        self.num_vm += 1

    def removeVM(self, vm):
        """ Remove a vm from the switch
        @vm: VM to remove
        """
        #TODO: Delete key from bf?
        self.vmDic.pop(vm.getid())
        self.bfDic.pop(vm.getid())
        self.num_vm -= 1
    
    def hasVM(self, vm):
        """ Check local sw list to see if the vm in group.
        @vm: VM to test
        @return: True or False
        """
        if vm.getid() in self.vmDic:
            return True
        else:
            return False
    
    def hasVMbyBF(self, vm):
        """ Check BF to see if the vm in group.
        @vm: VM to test
        @return: True or False
        """
        if vm.getid() in self.bf:
            return True
        else:
            return False
