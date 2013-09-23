package net.floodlightcontroller.pktinhistory;

import java.util.ArrayList;
import java.util.List;

import net.floodlightcontroller.core.types.SwitchMessagePair;

import org.restlet.resource.Get;

import org.restlet.resource.ServerResource;

public class PktInHistoryResource extends ServerResource {
	@Get("json")
    public List<SwitchMessagePair> retrieve() {
        IPktinHistoryService pihr = (IPktinHistoryService)getContext().getAttributes().get(IPktinHistoryService.class.getCanonicalName());
        List<SwitchMessagePair> l = new ArrayList<SwitchMessagePair>();
        l.addAll(java.util.Arrays.asList(pihr.getBuffer().snapshot()));
        return l;
    }
}
