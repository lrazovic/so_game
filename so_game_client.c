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


// World  variables
int window;
World world;
Vehicle *vehicle; // The vehicle

// Networking
uint16_t port_number;
int socket_desc = -1; // socket tcp
int socket_udp = -1;  // socket udp
struct timeval last_update_time;
struct timeval start_time;
pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;


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
  int my_id;
  Image *map_elevation;
  Image *map_texture;
  Image *my_texture_from_server;

  // todo: connect to the server
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
  my_id = getID(socket_desc);
  local_world->ids[0] = my_id;
  fprintf(stdout, "[Main] ID number %d received \n", my_id);
  Image* surface_elevation = getElevationMap(socket_desc);
  fprintf(stdout, "[Main] Map elevation received \n");
  Image* surface_texture = getTextureMap(socket_desc);
  fprintf(stdout, "[Main] Map texture received \n");
  debug_print("[Main] Sending vehicle texture");
  sendVehicleTexture(socket_desc, my_texture, id);
  fprintf(stdout, "[Main] Client Vehicle texture sent \n");

  // construct the world
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle = (Vehicle *)malloc(sizeof(Vehicle));
  Vehicle_init(&vehicle, &world, my_id, my_texture_from_server);
  World_addVehicle(&world, vehicle);

  // spawn a thread that will listen the update messages from
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
