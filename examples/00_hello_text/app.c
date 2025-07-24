#include <nerak.h>

config(app){
  context("greeting", "hello world");
  resource("home", "/", .mime = n_txt,
    .get = {
      respond("greeting")
    }
  );
}
