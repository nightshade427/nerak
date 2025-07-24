begin transaction;
update pokemons
 set wins = wins + 1
 where id = {{winner}};
update pokemons
 set loses = loses + 1
 where id = {{loser}};
commit;
