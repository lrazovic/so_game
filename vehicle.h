#pragma once
#include "image.h"
#include "linked_list.h"
#include "surface.h"
#include "pthread.h"
struct World;
struct Vehicle;
typedef void (*VehicleDtor)(struct Vehicle* v);

typedef struct Vehicle {
    ListItem list;
    int id;
    struct World* world;
    Image* texture;
    pthread_mutex_t mutex;
    // these are the forces that will be applied after the update
    float translational_force_update,translational_force_intention;
    float rotational_force_update,rotational_force_intention;
    
    float x, y, z, theta; //position and orientation of the vehicle, on the surface

    // dont' touch these
    float prev_x, prev_y, prev_z, prev_theta; //orientation of the vehicle, on the surface
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

int Vehicle_update(Vehicle* v, float dt);

void Vehicle_destroy(Vehicle* v);

void Vehicle_getXYTheta(Vehicle* v, float* x, float* y, float* theta);

void Vehicle_setXYTheta(Vehicle* v, float x, float y, float theta);

void Vehicle_getForcesUpdate(Vehicle* v, float* translational_update, float* rotational_update);

void Vehicle_setForcesUpdate(Vehicle* v, float translational_update, float rotational_update);

void Vehicle_setForcesIntention(Vehicle* v, float translational_update, float rotational_update);

void Vehicle_getForcesIntentionUpdate(Vehicle* v, float* translational_update, float* rotational_update);