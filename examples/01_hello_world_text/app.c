#include <nerak.h>
#include <http.h>

config(app){
  context("greeting", "hello {{name}}");
  http("home", "/", .mime = mime_text,
    .get = {
      input({"name", .default_value = "world"}),
      html("greeting", "hello"),
      http_response("hello")
    }
  );
}
