#include <nerak.h>
#include <http.h>
#include <sqlite.h>
#include <datastar.h>
#include <session_auth.h>

config(app){
  middleware(session());

  sqlite(
    "todos_db",
    "file:todo.db?mode=rwc",
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
    .sse = {"todos:{{user_id}}"},
    .get = {
      sqlite_query({"todos_db", "get_todos", "todos_data"}),
      html("todos", "todos_s"),
      http_response("todos_s")
    },
    .post = {
      input({"title", not_empty_input}),
      sqlite_query({"todos_db", "create_todo", "todo_data", .error_on_empty = true}),
      html("todo", "todo_s"),
      datastar("todos:{{user_id}}", .target = "todos", .mode = prepend_mode, .elements = "todo_s")
    }
  );

  http("todo", "/todos/:id",
    .all = {
      logged_in(),
      input({"id", positive_integer_input})
    },
    .patch = {
      input({"finished", "1", "must be 1", .optional = true}),
      sqlite_query({"todos_db", "update_todo", "todo_data", .error_on_empty = true}),
      html("todo", "todo_s"),
      datastar("todos:{{user_id}}", .target = "todo_{{id}}", .mode = replace_mode, .elements = "todo_s")
    },
    .delete = {
      sqlite_query({"todos_db", "delete_todo", .error_on_empty = true}),
      datastar("todos:{{user_id}}", .target = "todo_{{id}}", .mode = remove_mode)
    }
  );
}
