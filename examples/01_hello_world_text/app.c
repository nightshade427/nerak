#include <nerak.h>

config(app){
  context("greeting", "hello {{name}}");
  resource("home", "/", .mime = n_txt,
    .get = {
      input({"name", .def = "world"}),
      mustache("greeting", "hello"),
      respond("hello")
    }
  );
}
