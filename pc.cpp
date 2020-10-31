#include <stdlib.h>
#include <ncurses.h>
#include <string>

#include "dungeon.h"
#include "pc.h"
#include "utils.h"
#include "move.h"
#include "path.h"
#include "io.h"
#include "object.h"
#include "descriptions.h"

const char *eq_slot_name[num_eq_slots] = {
  "weapon",
  "offhand",
  "ranged",
  "light",
  "armor",
  "helmet",
  "cloak",
  "gloves",
  "boots",
  "amulet",
  "lh ring",
  "rh ring"
};

pc::pc()
{
  uint32_t i;

  for (i = 0; i < num_eq_slots; i++) {
    eq[i] = 0;
  }

  for (i = 0; i < MAX_INVENTORY; i++) {
    in[i] = 0;
  }
  talked_to_wizard = 0;
  speed_modifier = 0;
  hp = 1000;
}

pc::~pc()
{
  uint32_t i;

  for (i = 0; i < MAX_INVENTORY; i++) {
    if (in[i]) {
      delete in[i];
      in[i] = NULL;
    }
  }
    
  for (i = 0; i < num_eq_slots; i++) {
    if (eq[i]) {
      delete eq[i];
      eq[i] = NULL;
    }
  }
}

uint32_t pc_is_alive(dungeon_t *d)
{
  return d->PC && d->PC->alive;
}

void place_pc(dungeon_t *d)
{
  d->PC->position[dim_y] = rand_range(d->rooms->position[dim_y],
                                     (d->rooms->position[dim_y] +
                                      d->rooms->size[dim_y] - 1));
  d->PC->position[dim_x] = rand_range(d->rooms->position[dim_x],
                                     (d->rooms->position[dim_x] +
                                      d->rooms->size[dim_x] - 1));
  pc_init_known_terrain(d->PC);
  pc_observe_terrain(d->PC, d);

  io_display(d);
}

void config_pc(dungeon_t *d)
{
  static dice pc_dice(0, 1, 4);

  d->PC = new pc;

  d->PC->symbol = '@';

  place_pc(d);

  d->PC->wealth = 1000;
  
  d->PC->speed = PC_SPEED;
  d->PC->alive = 1;
  d->PC->sequence_number = 0;
  d->PC->kills[kill_direct] = d->PC->kills[kill_avenged] = 0;
  d->PC->color.push_back(COLOR_WHITE);
  d->PC->damage = &pc_dice;
  d->PC->name = "Isabella Garcia-Shapiro";

  d->character_map[character_get_y(d->PC)][character_get_x(d->PC)] = d->PC;

  dijkstra(d);
  dijkstra_tunnel(d);
}

uint32_t pc_next_pos(dungeon_t *d, pair_t dir)
{
  static uint32_t have_seen_corner = 0;
  static uint32_t count = 0;

  dir[dim_y] = dir[dim_x] = 0;

  if (in_corner(d, d->PC)) {
    if (!count) {
      count = 1;
    }
    have_seen_corner = 1;
  }

  /* First, eat anybody standing next to us. */
  if (charxy(d->PC->position[dim_x] - 1, d->PC->position[dim_y] - 1)) {
    dir[dim_y] = -1;
    dir[dim_x] = -1;
  } else if (charxy(d->PC->position[dim_x], d->PC->position[dim_y] - 1)) {
    dir[dim_y] = -1;
  } else if (charxy(d->PC->position[dim_x] + 1, d->PC->position[dim_y] - 1)) {
    dir[dim_y] = -1;
    dir[dim_x] = 1;
  } else if (charxy(d->PC->position[dim_x] - 1, d->PC->position[dim_y])) {
    dir[dim_x] = -1;
  } else if (charxy(d->PC->position[dim_x] + 1, d->PC->position[dim_y])) {
    dir[dim_x] = 1;
  } else if (charxy(d->PC->position[dim_x] - 1, d->PC->position[dim_y] + 1)) {
    dir[dim_y] = 1;
    dir[dim_x] = -1;
  } else if (charxy(d->PC->position[dim_x], d->PC->position[dim_y] + 1)) {
    dir[dim_y] = 1;
  } else if (charxy(d->PC->position[dim_x] + 1, d->PC->position[dim_y] + 1)) {
    dir[dim_y] = 1;
    dir[dim_x] = 1;
  } else if (!have_seen_corner || count < 250) {
    /* Head to a corner and let most of the NPCs kill each other off */
    if (count) {
      count++;
    }
    if (!against_wall(d, d->PC) && ((rand() & 0x111) == 0x111)) {
      dir[dim_x] = (rand() % 3) - 1;
      dir[dim_y] = (rand() % 3) - 1;
    } else {
      dir_nearest_wall(d, d->PC, dir);
    }
  }else {
    /* And after we've been there, let's head toward the center of the map. */
    if (!against_wall(d, d->PC) && ((rand() & 0x111) == 0x111)) {
      dir[dim_x] = (rand() % 3) - 1;
      dir[dim_y] = (rand() % 3) - 1;
    } else {
      dir[dim_x] = ((d->PC->position[dim_x] > DUNGEON_X / 2) ? -1 : 1);
      dir[dim_y] = ((d->PC->position[dim_y] > DUNGEON_Y / 2) ? -1 : 1);
    }
  }

  /* Don't move to an unoccupied location if that places us next to a monster */
  if (!charxy(d->PC->position[dim_x] + dir[dim_x],
              d->PC->position[dim_y] + dir[dim_y]) &&
      ((charxy(d->PC->position[dim_x] + dir[dim_x] - 1,
               d->PC->position[dim_y] + dir[dim_y] - 1) &&
        (charxy(d->PC->position[dim_x] + dir[dim_x] - 1,
                d->PC->position[dim_y] + dir[dim_y] - 1) != d->PC)) ||
       (charxy(d->PC->position[dim_x] + dir[dim_x] - 1,
               d->PC->position[dim_y] + dir[dim_y]) &&
        (charxy(d->PC->position[dim_x] + dir[dim_x] - 1,
                d->PC->position[dim_y] + dir[dim_y]) != d->PC)) ||
       (charxy(d->PC->position[dim_x] + dir[dim_x] - 1,
               d->PC->position[dim_y] + dir[dim_y] + 1) &&
        (charxy(d->PC->position[dim_x] + dir[dim_x] - 1,
                d->PC->position[dim_y] + dir[dim_y] + 1) != d->PC)) ||
       (charxy(d->PC->position[dim_x] + dir[dim_x],
               d->PC->position[dim_y] + dir[dim_y] - 1) &&
        (charxy(d->PC->position[dim_x] + dir[dim_x],
                d->PC->position[dim_y] + dir[dim_y] - 1) != d->PC)) ||
       (charxy(d->PC->position[dim_x] + dir[dim_x],
               d->PC->position[dim_y] + dir[dim_y] + 1) &&
        (charxy(d->PC->position[dim_x] + dir[dim_x],
                d->PC->position[dim_y] + dir[dim_y] + 1) != d->PC)) ||
       (charxy(d->PC->position[dim_x] + dir[dim_x] + 1,
               d->PC->position[dim_y] + dir[dim_y] - 1) &&
        (charxy(d->PC->position[dim_x] + dir[dim_x] + 1,
                d->PC->position[dim_y] + dir[dim_y] - 1) != d->PC)) ||
       (charxy(d->PC->position[dim_x] + dir[dim_x] + 1,
               d->PC->position[dim_y] + dir[dim_y]) &&
        (charxy(d->PC->position[dim_x] + dir[dim_x] + 1,
                d->PC->position[dim_y] + dir[dim_y]) != d->PC)) ||
       (charxy(d->PC->position[dim_x] + dir[dim_x] + 1,
               d->PC->position[dim_y] + dir[dim_y] + 1) &&
        (charxy(d->PC->position[dim_x] + dir[dim_x] + 1,
                d->PC->position[dim_y] + dir[dim_y] + 1) != d->PC)))) {
    dir[dim_x] = dir[dim_y] = 0;
  }

  return 0;
}

uint32_t pc_in_room(dungeon_t *d, uint32_t room)
{
  if ((room < d->num_rooms)                                     &&
      (d->PC->position[dim_x] >= d->rooms[room].position[dim_x]) &&
      (d->PC->position[dim_x] < (d->rooms[room].position[dim_x] +
                                d->rooms[room].size[dim_x]))    &&
      (d->PC->position[dim_y] >= d->rooms[room].position[dim_y]) &&
      (d->PC->position[dim_y] < (d->rooms[room].position[dim_y] +
                                d->rooms[room].size[dim_y]))) {
    return 1;
  }

  return 0;
}

void pc_learn_terrain(pc *p, pair_t pos, terrain_type_t ter)
{
  p->known_terrain[pos[dim_y]][pos[dim_x]] = ter;
  p->visible[pos[dim_y]][pos[dim_x]] = 1;
}

void pc_reset_visibility(pc *p)
{
  uint32_t y, x;

  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      p->visible[y][x] = 0;
    }
  }
}

terrain_type_t pc_learned_terrain(pc *p, int16_t y, int16_t x)
{
  if (y < 0 || y >= DUNGEON_Y || x < 0 || x >= DUNGEON_X) {
    io_queue_message("Invalid value to %s: %d, %d", __FUNCTION__, y, x);
  }

  return p->known_terrain[y][x];
}

void pc_init_known_terrain(pc *p)
{
  uint32_t y, x;

  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      p->known_terrain[y][x] = ter_unknown;
      p->visible[y][x] = 0;
    }
  }
}

void pc_observe_terrain(pc *p, dungeon_t *d)
{
  pair_t where;
  int16_t y_min, y_max, x_min, x_max;

  y_min = p->position[dim_y] - PC_VISUAL_RANGE;
  if (y_min < 0) {
    y_min = 0;
  }
  y_max = p->position[dim_y] + PC_VISUAL_RANGE;
  if (y_max > DUNGEON_Y - 1) {
    y_max = DUNGEON_Y - 1;
  }
  x_min = p->position[dim_x] - PC_VISUAL_RANGE;
  if (x_min < 0) {
    x_min = 0;
  }
  x_max = p->position[dim_x] + PC_VISUAL_RANGE;
  if (x_max > DUNGEON_X - 1) {
    x_max = DUNGEON_X - 1;
  }

  for (where[dim_y] = y_min; where[dim_y] <= y_max; where[dim_y]++) {
    where[dim_x] = x_min;
    can_see(d, p->position, where, 1, 1);
    where[dim_x] = x_max;
    can_see(d, p->position, where, 1, 1);
  }
  /* Take one off the x range because we alreay hit the corners above. */
  for (where[dim_x] = x_min - 1; where[dim_x] <= x_max - 1; where[dim_x]++) {
    where[dim_y] = y_min;
    can_see(d, p->position, where, 1, 1);
    where[dim_y] = y_max;
    can_see(d, p->position, where, 1, 1);
  }       
}

int32_t is_illuminated(pc *p, int16_t y, int16_t x)
{
  return p->visible[y][x];
}

void pc_see_object(character *the_pc, object *o)
{
  if (o) {
    o->has_been_seen();
  }
}

void pc::recalculate_speed()
{
  int i;

  for (speed = PC_SPEED +  speed_modifier, i = 0; i < num_eq_slots; i++) {
    if (eq[i]) {
      speed += eq[i]->get_speed();
    }
  }

  if (speed <= 0) {
    speed = 1;
  }
}

uint32_t pc::wear_in(uint32_t slot)
{
  object *tmp;
  uint32_t i;

  if (!in[slot] || !in[slot]->is_equipable()) {
    return 1;
  }

  /* Rings are tricky since there are two slots.  We will alwas favor *
   * an empty slot, and if there is no empty slot, we'll use the      *
   * first slot.                                                      */
  i = in[slot]->get_eq_slot_index();
  if (eq[i] &&
      ((eq[i]->get_type() == objtype_RING) &&
       !eq[i + 1])) {
    i++;
  }

  tmp = in[slot];
  in[slot] = eq[i];
  eq[i] = tmp;

  io_queue_message("You wear %s.", eq[i]->get_name());

  recalculate_speed();

  return 0;
}

uint32_t pc::has_open_inventory_slot()
{
  int i;

  for (i = 0; i < MAX_INVENTORY; i++) {
    if (!in[i]) {
      return 1;
    }
  }

  return 0;
}

int32_t pc::get_first_open_inventory_slot()
{
  int i;

  for (i = 0; i < MAX_INVENTORY; i++) {
    if (!in[i]) {
      return i;
    }
  }

  return -1;
}

uint32_t pc::remove_eq(uint32_t slot)
{
  if (!eq[slot]                      ||
      !in[slot]->is_removable() ||
      !has_open_inventory_slot()) {
    io_queue_message("You can't remove %s, because you have nowhere to put it.",
                     eq[slot]->get_name());

    return 1;
  }

  io_queue_message("You remove %s.", eq[slot]->get_name());

  in[get_first_open_inventory_slot()] = eq[slot];
  eq[slot] = NULL;


  recalculate_speed();

  return 0;
}

uint32_t pc::drop_in(dungeon_t *d, uint32_t slot)
{
  if (!in[slot] || !in[slot]->is_dropable()) {
    return 1;
  }

  io_queue_message("You drop %s.", in[slot]->get_name());

  in[slot]->to_pile(d, position);
  in[slot] = NULL;

  return 0;
}

uint32_t pc::destroy_in(uint32_t slot)
{
  if (!in[slot] || !in[slot]->is_destructable()) {
    return 1;
  }

  io_queue_message("You destroy %s.", in[slot]->get_name());

  delete in[slot];
  in[slot] = NULL;

  return 0;
}

uint32_t pc::pick_up(dungeon_t *d)
{
  object *o;

  while (has_open_inventory_slot() &&
         d->objmap[position[dim_y]][position[dim_x]]) {
    io_queue_message("You pick up %s.",
                     d->objmap[position[dim_y]][position[dim_x]]->get_name());
    in[get_first_open_inventory_slot()] =
      from_pile(d, position);
  }

  for (o = d->objmap[position[dim_y]][position[dim_x]];
       o;
       o = o->get_next()) {
    io_queue_message("You have no room for %s.", o->get_name());
  }

  return 0;
}

object *pc::from_pile(dungeon_t *d, pair_t pos)
{
  object *o;

  if ((o = (object *) d->objmap[pos[dim_y]][pos[dim_x]])) {
    d->objmap[pos[dim_y]][pos[dim_x]] = o->get_next();
    o->set_next(0);
  }

  return o;
}

void pc::check_gold(dungeon_t *d)
{
  int i;

  for (i = 0; i < MAX_INVENTORY; i++) {
    if (in[i] && in[i]->get_type() == objtype_GOLD) {
      d->PC->wealth += in[i]->get_value();
      delete in[i];
      in[i] = NULL;
    }
  }
}

void pc::talk_wizard(dungeon_t *d, pair_t wizard)
{
  int i, j, correct = 0;
  char key;
  
  clear();
  mvprintw(0, 0, "                       ..,,**,,                        ");
  mvprintw(1, 0, "                     ,*/(#######(*,                    ");
  mvprintw(2, 0, "                    *((##%%%%%%%%%%%%%%##(,                   ");
  mvprintw(3, 0, "                   */(####%%%%%%%%%%###((,                  ");
  mvprintw(4, 0, "                 ,*/(#####%%%%%%%%%%%%###(/,                 ");
  mvprintw(5, 0, "                 ,**(#(######%#(###(*,                 ");
  mvprintw(6, 0, "                //**(#(//*#((#((/(*(*,                 ");
  mvprintw(7, 0, "                ##(/(##%%####((((####//*                ");
  mvprintw(8, 0, "                (#(/((###%#(#(((#%%##//                 ");
  mvprintw(9, 0, "                ,##/((##%%%%##(##((#%#//                 ");
  mvprintw(10, 0, "                   *((##%#(((((/(#%(*                  ");
  mvprintw(11, 0, "                   *//(########((##(                   ");
  mvprintw(12, 0, "                   *//((########((/                    ");
  mvprintw(13, 0, "                   /((//(((#####(*,                    ");
  mvprintw(14, 0, "                 ,*/###((////((/**                     ");
  mvprintw(15, 0, "              ,,,*((#######((((///                     ");
  mvprintw(16, 0, "          ,,,,,,,,,(#####%%####(((*,,,,                 ");
  mvprintw(17, 0, "     ,,,,,,,,,,,,,,,**/(((#((/****,,,,,,,,,            ");
  mvprintw(18, 0, "  ,,,,,,,,,,,,,,,,,,,,,************,,,,,,,,,,,,        ");
  mvprintw(19, 0, ",,,,,,,,,,,,,,,,,,,,,,,,,,,,*,,,*,,,,,,,,,,,,,,,,      ");

  mvprintw(21, 0, "YOU APPROACH THE ALL MIGHTY WIZARD                     ");
  mvprintw(22, 0, "  --Enter any key to continue--");
  refresh();
  getch();

  if(wealth < 1000){
    mvprintw(21, 0, "\"YOU DARE BRING ME LESS THAN 1000 PIECES OF GOLD?\"     ");
    mvprintw(22, 0, "  --Enter any key to continue--");
    refresh();
    getch();
  } else {
    correct = 1;
    mvprintw(21, 0, "\"YOU HAVE BROUGHT ME THE 1000 PIECES OF GOLD I REQUIRE\"");
    mvprintw(22, 0, "  --Enter any key to continue--");
    refresh();
    getch();

    mvprintw(21, 0, "\"NOW ANSWER THESE QUESTIONS THREE TO CLAIM YOUR PRIZE\" ");
    mvprintw(22, 0, "  --Enter any key to continue--");
    refresh();
    getch();
  }

  if(correct){
    mvprintw(21, 0, "\"WHAT IS YOUR NAME?\"                                  ");
    mvprintw(22, 0, "  --Enter A, B, or C to answer--");
    mvprintw(1, 60, "A. E. Xplorer");
    mvprintw(9, 60, "B. Isabella");
    mvprintw(10, 63, "Garcia-Shapiro");
    mvprintw(16, 60, "C. Vulfpeck");
    
    refresh();
    key = getch();
    while(key != 'A' && key != 'B' && key != 'C'){
      key = getch();
    }
    if(key == 'B'){
      mvprintw(21, 0, "\"CORRECT!\"                                  ");
      mvprintw(22, 0, "  --Enter any key to continue--");
      refresh();
      getch();
    } else {
      mvprintw(21, 0, "\"INCORRECT!\"                                  ");
      mvprintw(22, 0, "  --Enter any key to continue--");
      refresh();
      correct = 0;
      getch();
    }
  }

  if(correct){
    mvprintw(21, 0, "\"WHAT IS YOUR QUEST?\"                                  ");
    mvprintw(22, 0, "  --Enter A, B, or C to answer--");

    mvprintw(1, 60, "A. To seek the      ");
    mvprintw(2, 63, "holy grail!         ");
    mvprintw(9, 60, "B. To just get      ");
    mvprintw(10, 63, "an A in the         ");
    mvprintw(11, 63, "class. Please.      ");
    mvprintw(16, 60, "C. To kill          ");
    mvprintw(17, 63, "that spongyboy      ");

    refresh();
    key = getch();
    while(key != 'A' && key != 'B' && key != 'C'){
      key = getch();
    }
    if(key == 'C'){
      mvprintw(21, 0, "\"CORRECT!\"                                  ");
      mvprintw(22, 0, "  --Enter any key to continue--");
      refresh();
      getch();
    } else {
      mvprintw(21, 0, "\"INCORRECT!\"                                  ");
      mvprintw(22, 0, "  --Enter any key to continue--");
      refresh();
      correct = 0;
      getch();
    }
  }

  if(correct){
    mvprintw(21, 0, "\"WHAT IS THE POINT OF THIS ASSIGNMENT?\"                 ");
    mvprintw(22, 0, "  --Enter A, B, or C to answer--");

    mvprintw(1, 60, "A. To finish the  ");
    mvprintw(2, 63, "semester strong   ");
    mvprintw(9, 60, "B. To prove that  ");
    mvprintw(10, 63, "we are worthy     ");
    mvprintw(11, 63, "                  ");
    mvprintw(16, 60, "C. To understand  ");
    mvprintw(17, 63, "what a g*dd*mn    ");
    mvprintw(18, 63, "pointer is        ");
    
    refresh();
    key = getch();
    while(key != 'A' && key != 'B' && key != 'C'){
      key = getch();
    }
    if(key == 'B'){
      mvprintw(21, 0, "\"CORRECT!\"                                  ");
      mvprintw(22, 0, "  --Enter any key to continue--");
      refresh();
      getch();
    } else {
      mvprintw(21, 0, "\"INCORRECT!\"                                  ");
      mvprintw(22, 0, "  --Enter any key to continue--");
      refresh();
      correct = 0;
      getch();
    }
  }

  if(correct){
    mvprintw(21, 0, "\"YOU HAVE SUCCEEDED! AS A REWARD, I EMBUE YOU WITH IMMENSE STRENGTH!\"");
    mvprintw(22, 0, "  --Enter any key to continue--");
    static dice pc_dice(1000, 10, 10);
    damage = &pc_dice;
    hp += 1000;
    wealth -= 1000;
    speed_modifier += 100;
    refresh();
    getch();
  } else {
    mvprintw(21, 0, "\"YOU HAVE FAILED ME! I WILL RELINQUISH YOU OF ALL YOUR BELONGINGS!\"  ");
    mvprintw(22, 0, "  --Enter any key to continue--");
    wealth = 0;
    hp = 100;
    for(i = 0; i < MAX_INVENTORY; i++){
      if(in[i]){
	delete in[i];
	in[i] = NULL;
      }
    }
    for(j = 0; j < num_eq_slots; j++){
      if(eq[j]){
	delete eq[j];
	eq[j] = NULL;
      }
    }
    refresh();
    getch();
  }

  talked_to_wizard = 1;
  recalculate_speed();
  mappair(wizard) = ter_floor_room;
  hardnesspair(wizard) = 0;
  io_display(d);
}

uint32_t pc::talk(dungeon_t *d, uint32_t dir){
  pair_t p;

  p[dim_x] = d->PC->position[dim_x];
  p[dim_y] = d->PC->position[dim_y];

  switch (dir) {
  case 1:
  case 2:
  case 3:
    p[dim_y]++;
    break;
  case 4:
  case 5:
  case 6:
    break;
  case 7:
  case 8:
  case 9:
    p[dim_y]--;
    break;
  }
  switch (dir) {
  case 1:
  case 4:
  case 7:
    p[dim_x]--;
    break;
  case 2:
  case 5:
  case 8:
    break;
  case 3:
  case 6:
  case 9:
    p[dim_x]++;
    break;
  }

  if(!charpair(p) && mappair(p) != ter_wizard){
    io_queue_message("No NPC was there to talk to");
    return 0;
  }

  if(mappair(p) == ter_wizard){
    //io_queue_message("You talked to The Mighty Wizard");
    d->PC->talk_wizard(d, p);
  } else if (charpair(p)){
    io_queue_message("You talked to %s", charpair(p)->name);
    
    if(!(((npc *)charpair(p))->characteristics & NPC_SMART)){
      io_queue_message("The %s does not understand what you're trying to say", charpair(p)->name);
      io_queue_message("");
      return 0;
    } else {
      if(((npc *)charpair(p))->bribed){
	io_queue_message("You have already bribed the %s", charpair(p)->name);
	io_queue_message("");
	if(((npc *)charpair(p))->characteristics & NPC_TELEPATH){
	  io_queue_message("The telepathic %s reveals the dungeon to you", charpair(p)->name);
	  io_queue_message("");
	  io_display_no_fog(d);
	}
	return 0;
      }
      if(d->PC->wealth >= ((npc *)charpair(p))->greed){
	d->PC->wealth -= ((npc *)charpair(p))->greed;
	charpair(p)->wealth += ((npc *)charpair(p))->greed;
	if(!((npc *)charpair(p))->betrayed){
	  ((npc *)charpair(p))->bribed = 1;	
	  io_queue_message("You bribed the %s %d gold to fight for you", charpair(p)->name, ((npc *)charpair(p))->greed);
	  io_queue_message("");
	  if(((npc *)charpair(p))->characteristics & NPC_TELEPATH){
	    io_queue_message("The telepathic %s reveals the dungeon to you", charpair(p)->name);
	    io_queue_message("");
	    io_display_no_fog(d);
	  }
	} else {
	  io_queue_message("The %s gladly accepts your money,", charpair(p)->name);
	  io_queue_message("but they will never forgive your betrayal");
	  io_queue_message("");
	}
      } else {
	io_queue_message("You do not have enough gold to bribe the %s", charpair(p)->name);
	io_queue_message("");
      }
    }
  }
  
  return 0;
}

uint32_t pc::push(dungeon_t *d, uint32_t dir)
{
  pair_t p;

  p[dim_x] = d->PC->position[dim_x];
  p[dim_y] = d->PC->position[dim_y];

  switch (dir) {
  case 1:
  case 2:
  case 3:
    p[dim_y]++;
    break;
  case 4:
  case 5:
  case 6:
    break;
  case 7:
  case 8:
  case 9:
    p[dim_y]--;
    break;
  }
  switch (dir) {
  case 1:
  case 4:
  case 7:
    p[dim_x]--;
    break;
  case 2:
  case 5:
  case 8:
    break;
  case 3:
  case 6:
  case 9:
    p[dim_x]++;
    break;
  }

  if(!charpair(p)){
    io_queue_message("There was no ally to push out of your way");
    io_queue_message("");
    return 0;
  } else if (!((npc *)charpair(p))->bribed){
    io_queue_message("The %s won't budge for the likes of you", charpair(p)->name);
    io_queue_message("");
  } else {
    io_queue_message("You switched places with your %s", charpair(p)->name);
    io_queue_message("");
    charpair(p)->position[dim_y] = d->PC->position[dim_y];
    charpair(p)->position[dim_x] = d->PC->position[dim_x];
    charpair(d->PC->position) = charpair(p);
    d->PC->position[dim_y] = p[dim_y];
    d->PC->position[dim_x] = p[dim_x];
    charpair(p) = d->PC; 
  }

  return 0;
}
