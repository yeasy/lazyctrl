The LazyCtrl project
=================================

*Self-adaptive performance-tunning control architecture for SDN networks*

* version: 0.2
* author: Baohua Yang <mailto:baohyang@cn.ibm.com>, Kai Zheng <mailto:zhengkai@cn.ibm.com>
* see: <https://github.com/yeasy/lazyctrl>

## File Organization

### CCM
Includes the related code of a CCM module. CCM utilized floodlight as the SDN controller, while daemons are responsible to maintain the grouping in the networks, and are also utilized to communicate between CCM and DCMs.

### DCM
DCM is designed based on the openvswitch code. Specific DCM is designated as a DDCM. Besides, DCM runs agent daemons to report its state.

###Others
A test platform to check the functions of grouping algorithm, large-scale performance, etc.

##Installation

###Basic Requirements:
Enviroment that can normally 

* Support openvswitch (see <http://openvswitch.org>).
* Support Floodlight (see <http://www.projectfloodlight.org/floodlight>).
* Physical switches that support multi-cast.
* Server (as controller) can login into the servers (as edge switches, with 2 separate network interfaces) running ovs via ssh without authorization (Need to put the public authorization key previously).

####Sample Testbed:
![ScreenShot](Others/testbed.png)

* Three edge switches instances are connected through a physical switch (IP communicable), and are connected to the controller.
* Deploy DCM at every edge switches (DCM 1 - DCM 2), following the installation documents inside. Modify the address information as necessary. For example, control layer IP of DCM 1 is ```192.168.57.10```.
* Start all daemons in CCM as the controller, including the floodlight and the group manager daemons.

## How does it work?
The floodlight+openvswitch cooperate as the basic control-datapath model in SDN. Based on them, we enhanced openvswitch to behave as DCM/DDCM, while floodlight+daemons work as the CCM.


##Documentation
Besides the readme files, you can easily make the API document with python/java/c doc tools. Plenty comments in the code would also be helpful.

##Support
Feel free to email to the authors. The project is planed to be community-supported soon.


##Contributing
There are still numbers of components available to optimize. For example, to build more robust message switching system. 

Everyone is encouraged to download the code, examine it, modify it, and submit bug reports, bug fixes, feature requests, new features and other issues and pull requests. Thanks to everyone who has contributed to the project.
