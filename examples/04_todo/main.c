#include <nerak.h>
#include <sqlite.h>
#include <session_auth.h>

config(main){
  middleware(session());

  sqlite_database(
    .name ="todos_db",
    .connect = "file:{{user_id}}_todo.db?mode=rwc",
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
    .get = {
      sqlite_query({"todos_db", "get_todos", "todos_data"}),
      mustache("todos", "todos_s"),
      respond("todos_s")
    },
    .post = {
      input({"title", m_not_empty}),
      sqlite_query({"todos_db", "create_todo"}),
      redirect("todos")
    }
  );

  resource("todo", "/todos/:id",
    .all = {
      logged_in(),
      input({"id", m_positive})
    },
    .patch = {
      input({"finished", "1",  "must be 1", .optional = true,}),
      sqlite_query({"todos_db", "update_todo"}),
      redirect("todos")
    },
    .delete = {
      sqlite_query({"todos_db", "delete_todo"}),
      redirect("todos")
    }
  );
}
