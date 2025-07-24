#include <nerak.h>

config(app){
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
      input({"name", .def = "world"}),
      mustache("greeting", "hello"),
      respond("hello")
    }
  );
}
