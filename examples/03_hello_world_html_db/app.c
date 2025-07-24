#include <nerak.h>
#include <sqlite.h>

config(main){
  sqlite_config(
    "hello_db",
    "file:hello.db?mode=rwc",
    {"create_hello_world_table"},
    {"seed_hello_world"}
  );

  resource("home", "/",
    .get = {
      sqlite_query({"hello_db", "get_hello_world", "hello_world_data"}),
      mustache("hello_world", "hello"),
      respond("hello")
    }
  );
}
