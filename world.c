#include "world.h"
#include <GL/glut.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "common.h"
#include "image.h"
#include "surface.h"
#include "vehicle.h"

void World_destroy(World* w) {
  Surface_destroy(&w->ground);
  sem_t sem = w->vehicles.sem;
  sem_wait(&(sem));
  ListItem* item = w->vehicles.first;
  while (item) {
    Vehicle* v = (Vehicle*)item;
    Vehicle_destroy(v);
    item = item->next;
    free(v);
  }
  sem_post(&(sem));
  sem_destroy(&sem);
}

int World_init(World* w, Image* surface_elevation, Image* surface_texture) {
  List_init(&w->vehicles);
  Image* float_image = Image_convert(surface_elevation, FLOATMONO);

  if (!float_image) return 0;

  Surface_fromMatrix(&w->ground, (float**)float_image->row_data,
                     float_image->rows, float_image->cols, .5, .5, 5);
  w->ground.texture = surface_texture;
  Image_free(float_image);
  w->dt = 1;
  w->time_scale = 10;
  gettimeofday(&w->last_update, 0);
  return 1;
}

void World_update(World* w) {
  struct timeval current_time;
  gettimeofday(&current_time, 0);
  struct timeval dt;
  timersub(&current_time, &w->last_update, &dt);
  float delta = dt.tv_sec + 1e-6 * dt.tv_usec;
  float exp = delta / (30000 * 1e-6);
  float tr_decay = powf(1 - 0.001, exp);
  float rt_decay = powf(1 - 0.15, exp);
  sem_t sem = w->vehicles.sem;
  sem_wait(&sem);
  ListItem* item = w->vehicles.first;
  while (item) {
    Vehicle* v = (Vehicle*)item;
    pthread_mutex_lock(&v->mutex);
    if (!Vehicle_update(v, delta * w->time_scale)) {
      Vehicle_reset(v);
    } else {
      char flag = 0;
      if (!flag) {
        v->is_new = 0;
        v->temp_x = v->x;
        v->temp_y = v->y;
      }
      if (v->manual_updated) {
        struct timeval dt_manual;
        timersub(&current_time, &v->world_update_time, &dt_manual);
        float exp_manual = delta / (30000 * 1e-6);
        float tr_decay_manual = powf(1 - 0.001, exp_manual);
        float rt_decay_manual = powf(1 - 0.15, exp_manual);
        Vehicle_decayForcesUpdate(v, tr_decay_manual, rt_decay_manual);
        v->manual_updated = 0;
      } else {
        Vehicle_decayForcesUpdate(v, tr_decay, rt_decay);
      }
    }
    Vehicle_setTime(v, current_time);
    pthread_mutex_unlock(&v->mutex);
    item = item->next;
  }
  sem_post(&sem);
  w->last_update = current_time;
}

void World_manualUpdate(World* w, Vehicle* v, struct timeval update_time) {
  struct timeval current_time;
  gettimeofday(&current_time, 0);
  struct timeval dt;
  timersub(&current_time, &update_time, &dt);
  float delta = dt.tv_sec + 1e-6 * dt.tv_usec;
  float exp = delta / (30000 * 1e-6);
  float tr_decay = powf(1 - 0.001, exp);
  float rt_decay = powf(1 - 0.15, exp);
  if (!Vehicle_update(v, delta * w->time_scale)) Vehicle_reset(v);
  Vehicle_decayForcesUpdate(v, tr_decay, rt_decay);
  Vehicle_setTime(v, current_time);
  v->manual_updated = 1;
}

Vehicle* World_getVehicle(World* w, int vehicle_id) {
  sem_t sem = w->vehicles.sem;
  sem_wait(&sem);
  ListItem* item = w->vehicles.first;
  while (item) {
    Vehicle* v = (Vehicle*)item;
    if (v->id == vehicle_id) {
      sem_post(&sem);
      return v;
    }
    item = item->next;
  }
  sem_post(&sem);
  return 0;
}

Vehicle* World_addVehicle(World* w, Vehicle* v) {
  assert(!World_getVehicle(w, v->id));
  return (Vehicle*)List_insert(&w->vehicles, w->vehicles.last, (ListItem*)v);
}

Vehicle* World_detachVehicle(World* w, Vehicle* v) {
  return (Vehicle*)List_detach(&w->vehicles, (ListItem*)v);
}
