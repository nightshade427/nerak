#include <nerak.h>
#include <http.h>

config(app){
  context("greeting", "hello world");
  http("home", "/", .mime = mime_text,
    .get = {
      http_response("greeting")
    }
  );
}
