#include <nerak.h>
#include <session_auth.h>

config(landing){
  middleware(session());

  resource("home", "/",
    .get = {
      mustache("home", "home_s"),
      respond("home_s")
    }
  );
}
