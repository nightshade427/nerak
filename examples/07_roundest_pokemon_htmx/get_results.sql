select id, name, sprite, wins, loses,
 cast(wins as real) / nullif(loses, 0) as ratio_of_wins_to_loses,
 row_number() over (order by (cast(wins as real) / nullif(loses, 0)) desc) as rank
from pokemons
order by ratio_of_wins_to_loses desc;
