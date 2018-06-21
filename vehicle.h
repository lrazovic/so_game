#pragma once
#include <pthread.h>
#include <semaphore.h>
#include "image.h"
#include "linked_list.h"
#include "surface.h"
struct World;
struct Vehicle;
typedef void (*VehicleDtor)(struct Vehicle* v);

typedef struct Vehicle {
  ListItem list;
  int id;
  struct World* world;
  Image* texture;
  pthread_mutex_t mutex;
  struct timeval world_update_time;

  // these are the forces that will be applied after the update and the critical
  // section
  float translational_force_update;
  float rotational_force_update;
  float translational_force_update_intention;
  float rotational_force_update_intention;
  float x, y, z,
      theta;  // position and orientation of the vehicle, on the surface

  // dont' touch these
  char is_new, manual_updated;
  float temp_x, temp_y;
  float prev_x, prev_y, prev_z, prev_theta;  // orientation of the vehicle, on the surface
  float translational_velocity;
  float rotational_velocity;
  float translational_viscosity;
  float rotational_viscosity;
  float world_to_camera[16];
  float camera_to_world[16];
  float mass, angular_mass;
  float rotational_force, max_rotational_force, min_rotational_force;
  float translational_force, max_translational_force, min_translational_force;

  int gl_texture;
  int gl_list;
  VehicleDtor _destructor;
} Vehicle;

void Vehicle_init(Vehicle* v, struct World* w, int id, Image* texture);

void Vehicle_reset(Vehicle* v);

void Vehicle_getXYTheta(Vehicle* v, float* x, float* y, float* theta);

void Vehicle_setXYTheta(Vehicle* v, float x, float y, float theta);

int Vehicle_update(Vehicle* v, float dt);

void Vehicle_getForcesUpdate(Vehicle* v, float* translational_update,
                             float* rotational_update);

void Vehicle_addForcesUpdate(Vehicle* v, float translational_update,
                             float rotational_update);

void Vehicle_destroy(Vehicle* v);

void Vehicle_getForcesIntention(Vehicle* v,
                                float* translational_force_update_intention,
                                float* rotational_force_update_intention);

void Vehicle_setForcesIntention(Vehicle* v,
                                float translational_force_update_intention,
                                float rotational_force_update_intention);

void Vehicle_increaseTranslationalForceIntention(
    Vehicle* v, float translational_update_intention);

void Vehicle_increaseRotationalForceIntention(
    Vehicle* v, float rotational_update_intention);

void Vehicle_decreaseRotationalForceIntention(
    Vehicle* v, float rotational_update_intention);

void Vehicle_decreaseTranslationalForceIntention(
    Vehicle* v, float translational_update_intention);

void Vehicle_decayForcesUpdate(Vehicle* v, float translational_update_decay,
                               float rotational_update_decay);

int Vehicle_fixCollisions(Vehicle* v, Vehicle* v2);

void Vehicle_setForcesUpdate(Vehicle* v, float translational_force_update,
                             float rotational_force_update);

void Vehicle_setTime(Vehicle* v, struct timeval time);

void Vehicle_getTime(Vehicle* v, struct timeval* time);