#include <nerak.h>
#include <sqlite.h>
#include <datastar.h>
#include <session_auth.h>

config(app){
  middleware(session());

  sqlite_config(
    "todos_db",
    "file:todo.db?mode=rwc",
    {"create_todos_table"}
  );

  resource("home", "/",
    .get = {
      mustache("home", "home_s"),
      respond("home_s")
    }
  );

  resource("about", "/about",
    .get = {
      mustache("about", "about_s"),
      respond("about_s")
    }
  );

  resource("contact", "/contact",
    .get = {
      mustache("contact", "contact_s"),
      respond("contact_s")
    }
  );

  resource("todos", "/todos",
    .all = {logged_in()},
    .sse = {"todos:{{user_id}}"},
    .get = {
      sqlite_query({"todos_db", "get_todos", "todos_data"}),
      mustache("todos", "todos_s"),
      respond("todos_s")
    },
    .post = {
      input({"title", n_not_empty}),
      sqlite_query({"todos_db", "create_todo", "todo_data", .err_on_empty = true}),
      mustache("todo", "todo_s"),
      datastar("todos:{{user_id}}", .target = "todos", .mode = ds_prepend, .elements = "todo_s")
    }
  );

  resource("todo", "/todos/:id",
    .all = {
      logged_in(),
      input({"id", n_positive})
    },
    .patch = {
      input({"finished", "1", "must be 1", .opt = true}),
      sqlite_query({"todos_db", "update_todo", "todo_data", .err_on_empty = true}),
      mustache("todo", "todo_s"),
      datastar("todos:{{user_id}}", .target = "todo_{{id}}", .mode = ds_replace, .elements = "todo_s")
    },
    .delete = {
      sqlite_query({"todos_db", "delete_todo", .err_on_empty = true}),
      datastar("todos:{{user_id}}", .target = "todo_{{id}}", .mode = ds_remove)
    }
  );
}
