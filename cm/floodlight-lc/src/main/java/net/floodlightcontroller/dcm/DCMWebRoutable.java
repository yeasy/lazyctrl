package net.floodlightcontroller.dcm;

import org.restlet.Context;
import org.restlet.Restlet;
import org.restlet.routing.Router;

import net.floodlightcontroller.restserver.RestletRoutable;

public class DCMWebRoutable implements RestletRoutable {

    @Override
    public Restlet getRestlet(Context context) {
        Router router = new Router(context);
        router.attach("/table/{switch}/json", DCMTable.class);
        return router;
    }

    @Override
    public String basePath() {
        return "/wm/dcm";
    }
}
