#include <nerak.h>
#include <sqlite.h>
#include <datastar.h>
#include <session_auth.h>

config(main){
  middleware(session());

  sqlite_database(
    .name = "todos_db",
    .connect = "file:todo.db?mode=rwc",
    .migrations = {"create_todos_table"}
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
      input({"title", m_not_empty}),
      sqlite_query({"todos_db", "create_todo", "todo_data", .must_exist = true}),
      mustache("todo", "todo_s"),
      datastar_sse("todos:{{user_id}}", .target = "todos", .mode = mode_prepend, .elements = "todo_s")
    }
  );

  resource("todo", "/todos/:id",
    .all = {
      logged_in(),
      input({"id", m_positive})
    },
    .patch = {
      input({"finished", "1", "must be 1", .optional = true}),
      sqlite_query({"todos_db", "update_todo", "todo_data", .must_exist = true}),
      mustache("todo", "todo_s"),
      datastar_sse("todos:{{user_id}}", .target = "todo_{{id}}", .mode = mode_replace, .elements = "todo_s")
    },
    .delete = {
      sqlite_query({"todos_db", "delete_todo", .must_exist = true}),
      datastar_sse("todos:{{user_id}}", .target = "todo_{{id}}", .mode = mode_remove)
    }
  );
}
