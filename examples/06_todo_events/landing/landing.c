#include <nerak.h>
#include <http.h>
#include <session_auth.h>

config(landing){
  middleware(session());

  http("home", "/",
    .get = {
      html("home", "home_s"),
      http_response("home_s")
    }
  );
}
