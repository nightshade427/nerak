#include <nerak.h>
#include <http.h>
#include <sqlite.h>
#include <session_auth.h>

config(app){
  middleware(session());

  sqlite(
    "todos_db",
    "file:{{user_id}}_todo.db?mode=rwc",
    {"create_todos_table"}
  );

  http("home", "/",
    .get = {
      html("home", "home_s"),
      http_response("home_s")
    }
  );

  http("about", "/about",
    .get = {
      html("about", "about_s"),
      http_response("about_s")
    }
  );

  http("contact", "/contact",
    .get = {
      html("contact", "contact_s"),
      http_response("contact_s")
    }
  );

  http("todos", "/todos",
    .all = {logged_in()},
    .get = {
      sqlite_query({"todos_db", "get_todos", "todos_data"}),
      html("todos", "todos_s"),
      http_response("todos_s")
    },
    .post = {
      input({"title", not_empty_input}),
      sqlite_query({"todos_db", "create_todo"}),
      http_redirect("todos")
    }
  );

  http("todo", "/todos/:id",
    .all = {
      logged_in(),
      input({"id", positive_integer_input})
    },
    .patch = {
      input({"finished", "1",  "must be 1", .optional = true}),
      sqlite_query({"todos_db", "update_todo"}),
      http_redirect("todos")
    },
    .delete = {
      sqlite_query({"todos_db", "delete_todo"}),
      http_redirect("todos")
    }
  );
}
