#include <nerak.h>

config(main){
  context("greeting", "hello {{name}}");
  resource("home", "/", .mime = m_txt,
    .get = {
      input({"name", .fallback = "world"}),
      mustache("greeting", "hello"),
      respond("hello")
    }
  );
}
