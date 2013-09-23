#coding:utf-8
#!/usr/bin/python

'''The Virtual Machine module for the HC project.
@version: 1.0
@author: U{Baohua Yang<mailto:yangbaohua@gmail.com>}
@created: Oct 16, 2011
@last update: Oct 18, 2011
@see: U{<https://github.com/yeasy/lazyctrl>}
'''

class VM:
    """ The Virtual Machine class. """
    def __init__(self, vmid, swid):
        """ Init.
        @vmid: vm vmid
        @swid: Switch vmid
        """
        self.vmid = vmid
        self.swid = swid

    def setid(self, vmid):
        """ Set a vmid for the vm.
        @vmid: vmid to set
        """
        self.vmid = vmid
        
    def getid(self):
        """ Get the vmid of the vm. 
        @return: Return the vmid
        """
        return self.vmid
    
    def setSWid(self, swid):
        """ Set a switch  vmid for the vm.
        @swid: swid to set
        """
        self.swid = swid
        
    def getSWid(self):
        """ Get the switch vmid of the vm. 
        @return: Return the switch vmid
        """
        return self.swid
