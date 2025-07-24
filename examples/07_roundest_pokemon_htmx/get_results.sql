select id, name, sprite, wins, losses,
 cast(wins as real) / nullif(losses, 0) as ratio_of_wins_to_losses,
 row_number() over (order by (cast(wins as real) / nullif(losses, 0)) desc) as rank
from pokemons
order by ratio_of_wins_to_losses desc;
