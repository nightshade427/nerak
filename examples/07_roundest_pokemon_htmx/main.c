#include <nerak.h>
#include <htmx.h>
#include <sqlite.h>
#include <tailwind.h>

config(main){
  sqlite_database(
    .name = "pokemon_db",
    .connect = "file::memory:?cache=shared",
    .migrations = {"create_pokemons_table"}
  );

  resource("home", "/",
    .get = {
      sqlite_query({"pokemon_db", "get_challengers", "challengers"}),
      run(^(){
        auto const t = get("challengers");
        auto const p0 = table_get(t, 0);
        auto const p1 = table_get(t, 1);
        record_set(p0, "opponent_id", record_get(p1, "id"));
        record_set(p1, "opponent_id", record_get(p0, "id"));
      }),
      mustache("home", "home_s"),
      respond("home_s")
    },
    .post = {
      input(
        {"winner", m_positive},
        {"loser", m_positive}
      ),
      sqlite_query({"pokemon_db", "vote"}),
      reroute("home")
    }
  );

  resource("result", "/results",
    .get = {
      sqlite_query({"pokemon_db", "get_results", "results"}),
      mustache("results", "results_s"),
      respond("results_s")
    }
  );
}
