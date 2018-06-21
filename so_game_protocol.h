#pragma once
#include <time.h>
#include "common.h"
#include "vehicle.h"

typedef enum {
  GetId = 0x1,
  GetTexture = 0x2,
  GetElevation = 0x3,
  PostTexture = 0x4,
  PostElevation = 0x5,
  WorldUpdate = 0x6,
  VehicleUpdate = 0x7,
  PostDisconnect = 0x8,
} Type;


typedef struct {
  Type type;
  int size;
} PacketHeader;

// sent from client to server to notify its intentions
typedef struct {
  PacketHeader header;
  float translational_force;
  float rotational_force;
} VehicleForces;

// sent from client to server to ask for an id (id=-1)
// sent from server to client to assign an id
typedef struct {
  PacketHeader header;
  int id;
} IdPacket;

// sent from client to server (with type=PostTexture),
// to send its texture
// sent from server to client
//       (with type=PostTexture and id=0) to assign the surface texture
//       (with type=PostElevation and id=0) to assign the surface texture
//       (with type=PostTexture and id>0) to assign the  texture to vehicle id
typedef struct {
  PacketHeader header;
  int id;
  Image* image;
} ImagePacket;

// sent from client to server, in udp to notify the updates
typedef struct {
  PacketHeader header;
  int id;
  float rotational_force;
  float translational_force;
  struct timeval time;
} VehicleUpdatePacket;

// block of the client updates, id of vehicle
// x,y,theta (read from vehicle id) are position of vehicle
// id is the id of the vehicle
typedef struct {
  int id;
  float x;
  float y;
  float theta;
  float rotational_force;
  float translational_force;
  struct timeval client_update_time, client_creation_time;
} ClientUpdate;

// server world update, send by server (UDP)
typedef struct {
  PacketHeader header;
  int num_vehicles;
  struct timeval time;
  ClientUpdate* updates;
} WorldUpdatePacket;


// converts a well formed packet into a string in dest.
// returns the written bytes
// h is the packet to write
int Packet_serialize(char* dest, const PacketHeader* h);

// returns a newly allocated packet read from the buffer
PacketHeader* Packet_deserialize(const char* buffer, int size);

// deletes a packet, freeing memory
void Packet_free(PacketHeader* h);
