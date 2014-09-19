/**
*    Copyright (c) 2008 The Board of Trustees of The Leland Stanford Junior
*    University
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

/**
 * @author David Erickson (daviderickson@cs.stanford.edu) - Mar 11, 2010
 */
package org.openflow.protocol.action;


import org.jboss.netty.buffer.ChannelBuffer;
import org.openflow.util.U16;

/**
 * @author David Erickson (daviderickson@cs.stanford.edu) - Mar 11, 2010
 * @author Rob Sherwood (rob.sherwood@stanford.edu)
 */
public class OFActionRemote extends OFAction implements Cloneable {
    public static int MINIMUM_LENGTH = 8+8;

    protected short port;//output port
    protected int	  ip;  //remote ip

    public OFActionRemote() {
        super.setType(OFActionType.REMOTE);
        super.setLength((short) MINIMUM_LENGTH);
    }

    /**
     * Create an Output Action specifying both the port AND
     * the snaplen of the packet to send out that port.
     * The length field is only meaningful when port == OFPort.OFPP_CONTROLLER
     * @param port
     * @param ip
     */

    public OFActionRemote(short port, int ip) {
        super();
        super.setType(OFActionType.REMOTE);
        super.setLength((short) MINIMUM_LENGTH);
        this.port = port;
        this.ip   = ip;
    }

    /**
     * Get the output port
     * @return
     */
    public short getPort() {
        return this.port;
    }

    /**
     * Set the output port
     * @param port
     */
    public OFActionRemote setPort(short port) {
        this.port = port;
        return this;
    }
    
    /**
     * Get the remote ip
     * @return
     */
    public int getIp() {
		return ip;
	}

    /**
     * Set the remote ip
     * @param ip
     */
    public OFActionRemote setIp(int ip) {
		this.ip = ip;
		return this;
	}

    @Override
    public void readFrom(ChannelBuffer data) {
        super.readFrom(data);
        this.port = data.readShort();
        data.readShort();
        data.readInt();
        this.ip = data.readInt();
    }

    @Override
    public void writeTo(ChannelBuffer data) {
        super.writeTo(data);
        data.writeShort(port);
        data.writeShort((short) 0);
        data.writeInt(0);
        data.writeInt(ip);
    }

    @Override
    public int hashCode() {
        final int prime = 349;
        int result = super.hashCode();
        result = prime * result + port;
        result = prime * result + ip;
        return result;
    }

    @Override
    public boolean equals(Object obj) {
        if (this == obj) {
            return true;
        }
        if (!super.equals(obj)) {
            return false;
        }
        if (!(obj instanceof OFActionRemote)) {
            return false;
        }
        OFActionRemote other = (OFActionRemote) obj;
        if (port != other.port) {
            return false;
        }
        if (ip != other.ip) {
            return false;
        }
        return true;
    }

    /* (non-Javadoc)
     * @see java.lang.Object#toString()
     */
    @Override
    public String toString() {
        return "OFActionRemote [ip=" + ip
                + ", port=" + U16.f(port)
                + ", length=" + length + ", type=" + type + "]";
    }
}
