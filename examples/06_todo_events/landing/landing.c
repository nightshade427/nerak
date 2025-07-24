#include <nerak.h>
#include <http.h>
#include <cookie_auth.h>

config(landing){
  http("home", "/",
    .get = {
      cookie_session(),
      html("home", "home_s"),
      http_response("home_s")
    }
  );
}
