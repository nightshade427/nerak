#include <nerak.h>
#include <session_auth.h>
#include "todos/todos.c"
#include "activity/activity.c"

config(main){
  middleware(session());

  resource("home", "/",
    .get = {
      mustache("home", "home_s"),
      respond("home_s")
    }
  );
}
