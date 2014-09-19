#!/usr/bin/python
#called by groupManager
#Daemon script. Push some ms to remote host.
#Usage: python push_agent.py host_ip msg

import sys
import os
import string
import datetime
import urllib
import simplejson as json

#the user/ip of the remote host that needs to be monitored
user="root"
#remotehost="192.168.57.10"
remotefile="/tmp/lc_gid.dat"

if len(sys.argv) < 3:
    sys.exit('Usage: %s host_ip msg' % sys.argv[0])

remotehost=sys.argv[1]
msg=sys.argv[2]

work_path = os.path.split(os.path.realpath(__file__))[0]+"/"
os.system("cd "+work_path)

#cp remote file to local 
os.system("ssh %s@%s 'echo %s > %s' 2>&1 > /dev/null" %(user,remotehost,msg,remotefile))
