package net.floodlightcontroller.pktinhistory;

import org.restlet.Context;
import org.restlet.Restlet;
import org.restlet.routing.Router;

import net.floodlightcontroller.restserver.RestletRoutable;

public class PktInHistoryWebRoutable implements RestletRoutable {

	@Override
	public String basePath() {
        return "/wm/pktinhistory";
	}

	@Override
	public Restlet getRestlet(Context context) {
		Router router = new Router(context);
        router.attach("/history/json", PktInHistoryResource.class);
        return router;
	}

}
