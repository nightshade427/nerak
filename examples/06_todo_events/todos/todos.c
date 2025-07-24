#include <nerak.h>
#include <http.h>
#include <sqlite.h>
#include <pubsub.h>
#include <session_auth.h>

config(todos){
  middleware(logged_in(), session());

  sqlite(
    "todos_db",
    "file:todo.db?mode=rwc",
    {"create_todos_table"}
  );

  publish("todo_created",
    .with = {"user_id", "title"}
  );

  http("todos", "/todos",
    .get = {
      sqlite_query({"todos_db", "get_todos", "todos_data"}),
      html("todos", "todos_s"),
      http_response("todos_s")
    },
    .post = {
      input({"title", not_empty_input}),
      sqlite_query({"todos_db", "create_todo"}),
      emit("todo_created"),
      http_redirect("todos")
    }
  );

  http("todo", "/todos/:id",
    .all = {input({"id", positive_integer_input})},
    .patch = {
      input({"finished", "1", "must be 1", .optional = true}),
      sqlite_query({"todos_db", "update_todo"}),
      http_redirect("todos")
    },
    .delete = {
      sqlite_query({"todos_db", "delete_todo"}),
      http_redirect("todos")
    }
  );
}
