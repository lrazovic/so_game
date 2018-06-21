#pragma once
#include <sys/time.h>
#include "image.h"
#include "linked_list.h"
#include "surface.h"
#include "vehicle.h"

typedef struct World {
  ListHead vehicles;
  Surface ground;
  float dt;
  struct timeval last_update;
  float time_scale;
} World;

int World_init(World* w, Image* surface_elevation, Image* surface_texture);

void World_destroy(World* w);

void World_update(World* w);

void World_decayUpdate(World* w);

Vehicle* World_getVehicle(World* w, int vehicle_id);

Vehicle* World_addVehicle(World* w, Vehicle* v);

Vehicle* World_detachVehicle(World* w, Vehicle* v);

void World_manualUpdate(World* w, Vehicle* v, struct timeval update_time);