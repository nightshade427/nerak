#include <nerak.h>
#include <http.h>
#include <sqlite.h>
#include <pubsub.h>
#include <session_auth.h>

config(activity){
  middleware(logged_in(), session());

  sqlite(
    "activity_db",
    "file:activity.db?mode=rwc",
    {"create_activity_table"}
  );

  subscribe("todo_created", {
    sqlite_query({"activity_db", "insert_activity"})
  });

  http("activity", "/activity",
    .get = {
      sqlite_query({"activity_db", "get_activities", "activity"}),
      html("activity", "activity_s"),
      http_response("activity_s")
    }
  );
}
