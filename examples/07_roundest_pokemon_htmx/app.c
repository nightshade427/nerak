#include <nerak.h>
#include <htmx.h>
#include <sqlite.h>
#include <tailwind.h>

config(app){
  sqlite_config(
    "pokemon_db",
    "file::memory:?cache=shared",
    {"create_pokemons_table"}
  );

  resource("home", "/",
    .get = {
      sqlite_query({"pokemon_db", "get_challengers", "challengers"}),
      run(^(){
        auto const t = get("challengers");
        auto const p0 = tbl_get(t, 0);
        auto const p1 = tbl_get(t, 1);
        rec_set(p0, "opponent_id", rec_get(p1, "id"));
        rec_set(p1, "opponent_id", rec_get(p0, "id"));
      }),
      mustache("home", "home_s"),
      respond("home_s")
    },
    .post = {
      input(
        {"winner", n_positive},
        {"loser", n_positive}
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
