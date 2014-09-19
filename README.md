The LazyCtrl Project
=================================
*A hybrid control plane design for large-scale cloud data centers.*

* Version: 0.21
* Authors: [Baohua Yang](mailto:baohyang@cn.ibm.com), [Kai Zheng](mailto:zhengkai@cn.ibm.com)
* Homepage: <https://github.com/yeasy/lazyctrl>

## Download
SSH:
```
git clone git@github.com:yeasy/lazyctrl.git
```  
Https:
```
git clone https://github.com/yeasy/lazyctrl.git
```

## Organization

### Central Control Modules
The directory ```ccm```  contains essential centralized control modules that are responsible for handling flow-based centralized control, as well as local control group management, for LazyCtrl. 

* Central controller: Our implementation of the central controller in LazyCtrl is based on the [Floodight](http://www.projectfloodlight.org/floodlight) project, which we call ```floodlight-lc```.  We extend the basic [OpenFlow 1.0](http://archive.openflow.org/documents/openflow-spec-v1.0.0.pdf) protocol in ```floodlight-lc``` by introducing a function to support installing a new action *Encap*  for packet encapsulation and forwarding at edge switches. Besides, the central controller in LazyCtrl is only in charge of inter-group traffic flows.
* Switch grouping management: Some daemons are provided to maintain switch grouping and updating in the network, as well as providing communication channels between the central controller and local control groups.
* The grouping process is based on [METIS](http://glaros.dtc.umn.edu/gkhome/metis/metis/overview) project (version 5.0.2) and the gpmetis binary is suggested to be compiled on your own platform. 

### Local Control Modules
All the modules for local control in switch groups are contained under directory ```lcm```. Our implementation of flow switch (called ```openvswitch-lc```) is based on the [Open vSwitch](http://openvswitch.org) project and we extend Open vSwitch by implementing the *Encap* action. Besides, the *ovsd* module in ```openvswitch-lc``` will take the charge of maintaining L-FIB and G-FIB, while the *datapath* kernel module is also modified to provide essential forwarding functions.

We also implement some agent daemons at every flow switch to report its state to the central controller. 

###Others
A test platform to test the performance of the grouping algorithm, workload and packet forwarding delay in LazyCtrl.

##Installation

###Basic Requirements:

* Servers supporting [OpenvSwitch](http://openvswitch.org), e.g., most Linux based servers.
* Servers supporting [Floodlight Controller](http://www.projectfloodlight.org/floodlight), e.g., most Linux based servers.
* Servers supporting [METIS](http://glaros.dtc.umn.edu/gkhome/metis/metis/overview), e.g., most Linux based servers.
* Physical switches that support IP multi-cast.
* Server (as controller) can login into the servers (as edge switches, with at least 2 separate network interfaces) running OpenvSwtich via ssh without authorization (Need to put the public authorization key previously).

####Sample Testbed:
![ScreenShot](others/res/testbed.png)

* Three edge switch instances (as DCM of two groups) are connected through a physical switch (IP communicable), and are connected to the CCM.
    * Each DCM should have at least connect to two subnets: control layer subnet and the datapath layer subnet.
    * DCM can be installed at Linux based servers.
    * DCM can login into CCM via ssh without manual authorization.
* Deploy the DCM code at every edge switches (DCM 1 - DCM 3)
 *  following the [Installation documents](DCM/openvswitch-lc/INSTALL) inside. 
 *  Modify the address information as necessary. For example, the default control plane IP of DCM 1 is ```192.168.57.10```, while for DCM 2, the IP is ```192.168.57.11```. The datapath plane IP is set to ```10.0.0.0/8``` subnet.
 *  Start the agent.sh daemon at each DCM.
* Setup CCM at the controller server.
    * Start floodlight-lc  in CCM to receive PACKET_IN msg, see [Floodlight Getting Started](http://www.projectfloodlight.org/getting-started/). Basically, run ```ant; java -jar target/floodlight.jar``` if all dependencies are already satisfied.
    * Start the collect_agent.sh daemon to collect upward statistics.
    * Start the groupManager daemon to update the grouping.
* Test the deployment
    * Capture IP multicast message at each DCM, find they are broadcasting the LCG location/statistics information.
    * Ping between two hosts of the same LCG, it will be handled by the L-FIB, hence no to-controller openflow message is generated. The latency will be similar as directly transferring.
    * Ping between two hosts of different LCG, the to-controller openflow message is generated, and the CCM will handle it using the G-FIB. The latency would be much higher as DCM has to talk with CCM. Besides, the packetRemote message will be sent to DCM.

## How does it work?
Basically, the floodlight-lc+openvswitch-lc cooperate as the basic control-datapath model in SDN. Based on them, we enhanced openvswitch-lc+agent to behave as DCM/DDCM, while floodlight-lc+daemons work as the CCM. More details are discussed in the paper, e.g., the group maintain algorithm and the implementation technical issues.


##Documentation
Besides the readme files, you can easily make the API document with python/java/c doc tools. Plenty comments in the code would also be helpful.

##Support
Feel free to email to the authors. The project is planned to be community-supported soon.


##Contributing
There are still numbers of components available to optimize. For example, to build more robust message switching system. 

Everyone is encouraged to download the code, examine it, modify it, and submit bug reports, bug fixes, feature requests, new features and other issues and pull requests. Thanks to everyone who has contributed to the project.
