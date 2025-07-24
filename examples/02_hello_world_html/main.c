#include <nerak.h>

config(main){
  context("greeting",
    "<html>"
      "<head></head>"
      "<body>"
        "<p>Hello {{name}}</p>"
      "</body>"
    "</html>"
  );
  resource("home", "/",
    .get = {
      input({"name", .fallback = "world"}),
      mustache("greeting", "hello"),
      respond("hello")
    }
  );
}
