package org.openflow.protocol;

import org.junit.Test;

public class OFPacketRemoteTest {

    @Test(expected = IllegalArgumentException.class)
    public void testBothBufferIdAndPayloadSet() {
        OFPacketRemote packetRemote = new OFPacketRemote();
        packetRemote.setBufferId(12);
        packetRemote.setPacketData(new byte[] { 1, 2, 3 });
    }

    @Test
    public void testOnlyBufferIdSet() {
        OFPacketRemote packetRemote = new OFPacketRemote();
        packetRemote.setBufferId(12);
        packetRemote.setPacketData(null);
        packetRemote.setPacketData(new byte[] {});
        packetRemote.validate();
    }

    @Test(expected = IllegalStateException.class)
    public void testNeitherBufferIdNorPayloadSet() {
        OFPacketRemote packetRemote = new OFPacketRemote();
        packetRemote.setBufferId(OFPacketRemote.BUFFER_ID_NONE);
        packetRemote.setPacketData(null);
        packetRemote.validate();
    }

    @Test(expected = IllegalStateException.class)
    public void testNeitherBufferIdNorPayloadSet2() {
        OFPacketRemote packetRemote = new OFPacketRemote();
        packetRemote.setBufferId(OFPacketRemote.BUFFER_ID_NONE);
        packetRemote.setPacketData(new byte[] {});
        packetRemote.validate();
    }

    @Test(expected = IllegalStateException.class)
    public void testNeitherBufferIdNorPayloadSet3() {
        OFPacketRemote packetRemote = new OFPacketRemote();
        packetRemote.validate();
    }

}
