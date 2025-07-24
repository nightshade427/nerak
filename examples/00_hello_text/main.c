#include <nerak.h>

config(main){
  context("greeting", "hello world");
  resource("home", "/", .mime = m_txt,
    .get = {
      respond("greeting")
    }
  );
}
