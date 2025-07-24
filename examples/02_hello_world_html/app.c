#include <nerak.h>
#include <http.h>

config(app){
  context("greeting",
    "<html>"
      "<head></head>"
      "<body>"
        "<p>Hello {{name}}</p>"
      "</body>"
    "</html>"
  );
  http("home", "/",
    .get = {
      input({"name", .default_value = "world"}),
      html("greeting", "hello"),
      http_response("hello")
    }
  );
}
