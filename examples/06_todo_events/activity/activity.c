#include <nerak.h>
#include <sqlite.h>
#include <pubsub.h>
#include <session_auth.h>

config(activity){
  middleware(logged_in(), session());

  sqlite_database(
    .name = "activity_db",
    .connect = "file:activity.db?mode=rwc",
    .migrations = {"create_activity_table"}
  );

  subscribe("todo_created", {
    sqlite_query({"activity_db", "insert_activity"})
  });

  resource("activity", "/activity",
    .get = {
      sqlite_query({"activity_db", "get_activities", "activity"}),
      mustache("activity", "activity_s"),
      respond("activity_s")
    }
  );
}
