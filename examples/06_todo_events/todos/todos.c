#include <nerak.h>
#include <sqlite.h>
#include <pubsub.h>
#include <session_auth.h>

config(todos){
  middleware(logged_in(), session());

  sqlite_database(
    .name = "todos_db",
    .connect = "file:todo.db?mode=rwc",
    .migrations = {"create_todos_table"}
  );

  publish("todo_created",
    .with = {"user_id", "title"}
  );

  resource("todos", "/todos",
    .get = {
      sqlite_query({"todos_db", "get_todos", "todos_data"}),
      mustache("todos", "todos_s"),
      respond("todos_s")
    },
    .post = {
      input({"title", m_not_empty}),
      sqlite_query({"todos_db", "create_todo"}),
      emit("todo_created"),
      redirect("todos")
    }
  );

  resource("todo", "/todos/:id",
    .all = {input({"id", m_positive})},
    .patch = {
      input({"finished", "1", "must be 1", .optional = true}),
      sqlite_query({"todos_db", "update_todo"}),
      redirect("todos")
    },
    .delete = {
      sqlite_query({"todos_db", "delete_todo"}),
      redirect("todos")
    }
  );
}
