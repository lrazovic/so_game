#include <GLUT/glut.h>
#include <arpa/inet.h> 
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>

#include "image.h"
#include "common.h"
#include "surface.h"
#include "vehicle.h"
#include "world.h"
#include "so_game_protocol.h"


// World  variables
int window;
int id;
World world;
Vehicle *vehicle; // The vehicle

// Networking
uint16_t port_number;
int socket_desc = -1; // socket tcp
int socket_udp = -1;  // socket udp
struct timeval last_update_time;
struct timeval start_time;
pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;

// Mark - Network Functions

int getID(int socket_desc) {
  char buf_send[BUFFERSIZE];
  char buf_rcv[BUFFERSIZE];
  IdPacket* request = (IdPacket*)malloc(sizeof(IdPacket));
  PacketHeader ph;
  ph.type = GetId;
  request->header = ph;
  request->id = -1;
  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1) return -1;
  int bytes_sent = 0;
  int ret = 0;
  while (bytes_sent < size) {
    ret = send(socket_desc, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Can't send ID request");
    if (ret == 0) break;
    bytes_sent += ret;
  }
  Packet_free(&(request->header));
  int ph_len = sizeof(PacketHeader);
  int msg_len = 0;
  while (msg_len < ph_len) {
    ret = recv(socket_desc, buf_rcv + msg_len, ph_len - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(msg_len, "Cannot read from socket");
    msg_len += ret;
  }
  PacketHeader* header = (PacketHeader*)buf_rcv;
  size = header->size - ph_len;

  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket_desc, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(msg_len, "Cannot read from socket");
    msg_len += ret;
  }
  IdPacket* deserialized_packet =
      (IdPacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[Get Id] Received %d bytes \n", msg_len + ph_len);
  int id = deserialized_packet->id;
  Packet_free(&(deserialized_packet->header));
  return id;
}

int sendVehicleTexture(int socket, Image* texture, int id) {
  char buf_send[BUFFERSIZE];
  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));
  PacketHeader ph;
  ph.type = PostTexture;
  request->header = ph;
  request->id = id;
  request->image = texture;

  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1) return -1;
  int bytes_sent = 0;
  int ret = 0;
  while (bytes_sent < size) {
    ret = send(socket, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Can't send vehicle texture");
    if (ret == 0) break;
    bytes_sent += ret;
  }
  printf("[Vehicle texture] Sent bytes %d  \n", bytes_sent);
  return 0;
}

Image* getElevationMap(int socket) {
  char buf_send[BUFFERSIZE];
  char buf_rcv[BUFFERSIZE];
  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));
  PacketHeader ph;
  ph.type = GetElevation;
  request->header = ph;
  request->id = -1;
  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1) return NULL;
  int bytes_sent = 0;
  int ret = 0;

  while (bytes_sent < size) {
    ret = send(socket, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Can't send Elevation Map request");
    if (ret == 0) break;
    bytes_sent += ret;
  }

  printf("[Elevation request] Sent %d bytes \n", bytes_sent);
  int msg_len = 0;
  int ph_len = sizeof(PacketHeader);
  while (msg_len < ph_len) {
    ret = recv(socket, buf_rcv, ph_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }

  PacketHeader* incoming_pckt = (PacketHeader*)buf_rcv;
  size = incoming_pckt->size - ph_len;
  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }

  ImagePacket* deserialized_packet =
      (ImagePacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[Elevation request] Received %d bytes \n", msg_len + ph_len);
  Packet_free(&(request->header));
  Image* res = deserialized_packet->image;
  free(deserialized_packet);
  return res;
}

Image* getTextureMap(int socket) {
  char buf_send[BUFFERSIZE];
  char buf_rcv[BUFFERSIZE];
  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));
  PacketHeader ph;
  ph.type = GetTexture;
  request->header = ph;
  request->id = -1;
  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1) return NULL;
  int bytes_sent = 0;
  int ret = 0;
  while (bytes_sent < size) {
    ret = send(socket, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Send Error");
    if (ret == 0) break;
    bytes_sent += ret;
  }
  printf("[Texture Map Request] Inviati %d bytes \n", bytes_sent);
  int msg_len = 0;
  int ph_len = sizeof(PacketHeader);
  while (msg_len < ph_len) {
    ret = recv(socket, buf_rcv, ph_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }
  PacketHeader* incoming_pckt = (PacketHeader*)buf_rcv;
  size = incoming_pckt->size - ph_len;
  printf("[Texture Request] Size da leggere %d \n", size);
  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }
  ImagePacket* deserialized_packet =
      (ImagePacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[Texture Request] Ricevuto bytes %d \n", msg_len + ph_len);
  Packet_free(&(request->header));
  Image* res = deserialized_packet->image;
  free(deserialized_packet);
  return res;
}

// Mark - TCP/UDP Handler

// Mark - Main


int main(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: %s <server_address> <player texture>\n", argv[1]);
    exit(-1);
  }

  printf("loading texture image from %s ... ", argv[2]);
  Image *my_texture = Image_load(argv[2]);
  if (my_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  Image *my_texture_for_server;
  Image *map_elevation;
  Image *map_texture;
  Image *my_texture_from_server;

  // DONE: connect to the server
  //   -get ad id
  //   -send your texture to the server (so that all can see you)
  //   -get an elevation map
  //   -get the texture of the surface

  printf("[Main] Starting... \n");
  port_number = htons((uint16_t)8888);
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  in_addr_t ip_addr = inet_addr(SERVER_ADDRESS);
  ERROR_HELPER(socket_desc, "Cannot create socket \n");
  struct sockaddr_in server_addr = {0};
  server_addr.sin_addr.s_addr = ip_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = port_number;
  int reuseaddr_opt = 1;  // recover server if a crash occurs
  int ret = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt,
                       sizeof(reuseaddr_opt));
  ERROR_HELPER(ret, "Can't set SO_REUSEADDR flag");

  ret = connect(socket_desc, (struct sockaddr*)&server_addr,
                sizeof(struct sockaddr_in));
  ERROR_HELPER(ret, "Cannot connect to remote server \n");

  fprintf(stdout, "[Main] Starting ID,map_elevation,map_texture requests \n");
  // Get Id
  id = getID(socket_desc);
  fprintf(stdout, "[Main] ID number %d received \n", id);

    // Send
  sendVehicleTexture(socket_desc, my_texture, id);
  fprintf(stdout, "[Main] Client Vehicle texture sent \n");

  // Surface Eleveation
  Image* surface_elevation = getElevationMap(socket_desc);
  fprintf(stdout, "[Main] Map elevation received \n");

  // Surface Eleveation
  Image* surface_texture = getTextureMap(socket_desc);
  fprintf(stdout, "[Main] Map texture received \n");


  // construct the world
  World_init(&world, surface_elevation, surface_texture, 0.5, 0.5, 0.5);
  vehicle = (Vehicle *)malloc(sizeof(Vehicle));
  Vehicle_init(&vehicle, &world, id, my_texture);
  World_addVehicle(&world, vehicle);

  // TODO: spawn a thread that will listen the update messages from
  // the server, and sends back the controls
  // the update for yourself are written in the desired_*_force
  // fields of the vehicle variable
  // when the server notifies a new player has joined the game
  // request the texture and add the player to the pool

  /*FILLME*/

  WorldViewer_runGlobal(&world, vehicle, &argc, argv);

  // cleanup
  World_destroy(&world);
  return 0;
}
