/**
*    Copyright 2011, Big Switch Networks, Inc. 
*    Originally created by David Erickson, Stanford University
* 
*    Licensed under the Apache License, Version 2.0 (the "License"); you may
*    not use this file except in compliance with the License. You may obtain
*    a copy of the License at
*
*         http://www.apache.org/licenses/LICENSE-2.0
*
*    Unless required by applicable law or agreed to in writing, software
*    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
*    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
*    License for the specific language governing permissions and limitations
*    under the License.
**/

package net.floodlightcontroller.core.types;

public class PortIpPair {
	public Short port;
    public int ip;
    
    public PortIpPair(Short port, int ip) {
        this.port = port;
        this.ip = ip;
    }
    
    public void setPort(Short port) {
		this.port = port;
	}

	public void setIp(int ip) {
		this.ip = ip;
	}

    
    public int getPort() {
        return port.shortValue();
    }
    
    public int getIp() {
        return ip;
    }
    
    public boolean equals(Object o) {
        return (o instanceof PortIpPair) && (port.equals(((PortIpPair) o).port))
            && (ip==(((PortIpPair) o).ip));
    }
    
    public int hashCode() {
        return port.hashCode() ^ ip;
    }
}
