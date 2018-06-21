#include "vehicle.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "world.h"
int Vehicle_update(Vehicle* v, float dt) {
  float tf = v->translational_force_update;
  float rf = v->rotational_force_update;
  if (tf > v->max_translational_force) tf = v->max_translational_force;
  if (tf < -v->max_translational_force) tf = -v->max_translational_force;
  if (rf > v->max_rotational_force) rf = v->max_rotational_force;
  if (rf < -v->max_rotational_force) rf = -v->max_rotational_force;
  float x, y, theta;
  x = v->x;
  y = v->y;
  theta = v->theta;
  // retrieve the position of the vehicle
  if (!Surface_getTransform(v->camera_to_world, &v->world->ground, x, y, 0,
                            theta, 0)) {
    v->translational_velocity = 0;
    v->rotational_velocity = 0;
    return 0;
  }

  // compute the new pose of the vehicle, based on the velocities
  // vehicle moves only along the x axis!
  float nx = v->camera_to_world[12] +
             v->camera_to_world[0] * v->translational_velocity * dt;
  float ny = v->camera_to_world[13] +
             v->camera_to_world[1] * v->translational_velocity * dt;
  if (!Surface_getTransform(v->camera_to_world, &v->world->ground, nx, ny, 0,
                            v->theta, 0)) {
    return 0;
  }

  v->x = v->camera_to_world[12];
  v->y = v->camera_to_world[13];
  v->z = v->camera_to_world[14];
  v->theta += v->rotational_velocity * dt;
  theta = v->theta;

  if (!Surface_getTransform(v->camera_to_world, &v->world->ground, nx, ny, 0,
                            theta, 0)) {
    return 0;
  }

  // compute the accelerations

  float global_tf = (-9.8 * v->camera_to_world[2] + tf);
  if (fabs(global_tf) < v->min_translational_force) global_tf = 0;
  v->translational_velocity += global_tf * dt;

  if (fabs(rf) < v->min_rotational_force) rf = 0;
  v->rotational_velocity += rf * dt;
  v->translational_velocity *= v->translational_viscosity;
  v->rotational_velocity *= v->rotational_viscosity;

  Surface_getTransform(v->world_to_camera, &v->world->ground, nx, ny, 0, theta,
                       1);
  return 1;
}

void Vehicle_init(Vehicle* v, World* w, int id, Image* texture) {
  int ret = pthread_mutex_init(&(v->mutex), NULL);
  if (ret == -1) printf("Mutex init for vehicle was not successfuf");

  v->world = w;
  v->id = id;
  v->texture = texture;
  v->theta = 0;
  v->list.next = v->list.prev = 0;
  v->x = v->world->ground.rows / 2 * v->world->ground.row_scale;
  v->y = v->world->ground.cols / 2 * v->world->ground.col_scale;
  v->translational_force_update = 0;
  v->rotational_force_update = 0;
  v->max_rotational_force = 0.5;
  v->max_translational_force = 10;
  v->min_rotational_force = 0.05;
  v->min_translational_force = 0.05;
  v->translational_velocity = 0;
  v->rotational_velocity = 0;
  v->translational_force_update_intention = 0;
  v->rotational_force_update_intention = 0;
  v->is_new = 1;
  v->manual_updated = 1;
  v->temp_x = v->x;
  v->temp_y = v->y;
  gettimeofday(&v->world_update_time, NULL);
  Vehicle_reset(v);
  v->gl_texture = -1;
  v->gl_list = -1;
  v->_destructor = 0;
}

void Vehicle_reset(Vehicle* v) {
  v->rotational_force = 0;
  v->translational_force = 0;
  v->x = v->world->ground.rows / 2 * v->world->ground.row_scale;
  v->y = v->world->ground.cols / 2 * v->world->ground.col_scale;
  v->theta = 0;
  v->translational_viscosity = 0.5;
  v->rotational_viscosity = 0.5;
  float x, y, theta;
  x = v->x;
  y = v->y;
  theta = v->theta;
  if (!Surface_getTransform(v->camera_to_world, &v->world->ground, x, y, 0,
                            theta, 0))
    return;
  return;
}

void Vehicle_getXYTheta(Vehicle* v, float* x, float* y, float* theta) {
  *x = v->x;
  *y = v->y;
  *theta = v->theta;
}

void Vehicle_setXYTheta(Vehicle* v, float x, float y, float theta) {
  v->x = x;
  v->y = y;
  v->theta = theta;
}

void Vehicle_getForcesUpdate(Vehicle* v, float* translational_update,
                             float* rotational_update) {
  *translational_update = v->translational_force_update;
  *rotational_update = v->rotational_force_update;
}

void Vehicle_addForcesUpdate(Vehicle* v, float translational_update,
                             float rotational_update) {
  v->translational_force_update += translational_update;
  v->rotational_force_update += rotational_update;
}

void Vehicle_destroy(Vehicle* v) {
  int ret = pthread_mutex_destroy(&(v->mutex));
  if (ret == -1) printf("Vehicle's mutex wasn't successfully destroyed");
  if (v->_destructor) (*v->_destructor)(v);
}

void Vehicle_getForcesIntention(Vehicle* v,
                                float* translational_force_update_intention,
                                float* rotational_force_update_intention) {
  *translational_force_update_intention =
      v->translational_force_update_intention;
  *rotational_force_update_intention = v->rotational_force_update_intention;
}

void Vehicle_increaseTranslationalForceIntention(
    Vehicle* v, float translational_force_update_intention) {
  v->translational_force_update_intention +=
      translational_force_update_intention;
}

void Vehicle_increaseRotationalForceIntention(
    Vehicle* v, float rotational_force_update_intention) {
  v->rotational_force_update_intention += rotational_force_update_intention;
}

void Vehicle_decreaseRotationalForceIntention(
    Vehicle* v, float rotational_force_update_intention) {
  v->rotational_force_update_intention -= rotational_force_update_intention;
}

void Vehicle_decreaseTranslationalForceIntention(
    Vehicle* v, float translational_force_update_intention) {
  v->translational_force_update_intention -=
      translational_force_update_intention;
}

void Vehicle_setForcesIntention(Vehicle* v,
                                float translational_force_update_intention,
                                float rotational_force_update_intention) {
  v->translational_force_update_intention =
      translational_force_update_intention;
  v->rotational_force_update_intention = rotational_force_update_intention;
}

void Vehicle_setForcesUpdate(Vehicle* v, float translational_force_update,
                             float rotational_force_update) {
  v->translational_force_update = translational_force_update;
  v->rotational_force_update = rotational_force_update;
}

void Vehicle_decayForcesUpdate(Vehicle* v, float translational_update_decay,
                               float rotational_update_decay) {
  v->translational_force_update *= translational_update_decay;
  v->rotational_force_update *= rotational_update_decay;
}

void Vehicle_setTime(Vehicle* v, struct timeval time) {
  v->world_update_time = time;
}

void Vehicle_getTime(Vehicle* v, struct timeval* time) {
  *time = v->world_update_time;
}