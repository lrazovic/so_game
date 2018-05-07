#include <GLUT/glut.h>
#include <arpa/inet.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "image.h"
#include "so_game_protocol.h"
#include "surface.h"
#include "vehicle.h"
#include "world.h"

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

typedef struct net_args {
  struct sockaddr_in server_addr;
  int socket_udp;
  int socket_tcp;
} udp_args;

// Mark - Network Functions

int getID(int socket_desc) {
  char buf_send[BUFFERSIZE];
  char buf_rcv[BUFFERSIZE];
  IdPacket *request = (IdPacket *)malloc(sizeof(IdPacket));
  PacketHeader ph;
  ph.type = GetId;
  request->header = ph;
  request->id = -1;
  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1)
    return -1;
  int bytes_sent = 0;
  int ret = 0;
  printf("Pre While\n");
  while (bytes_sent < size) {
    ret = send(socket_desc, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(ret, "Can't send ID request");
    if (ret == 0)
      break;
    bytes_sent += ret;
  }
  Packet_free(&(request->header));
  int ph_len = sizeof(PacketHeader);
  int msg_len = 0;
  while (msg_len < ph_len) {
    ret = recv(socket_desc, buf_rcv + msg_len, ph_len - msg_len, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(msg_len, "Cannot read from socket");
    msg_len += ret;
  }
  PacketHeader *header = (PacketHeader *)buf_rcv;
  size = header->size - ph_len;

  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket_desc, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(msg_len, "Cannot read from socket");
    msg_len += ret;
  }
  IdPacket *deserialized_packet =
      (IdPacket *)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[Get Id] Received %d bytes \n", msg_len + ph_len);
  int id = deserialized_packet->id;
  Packet_free(&(deserialized_packet->header));
  return id;
}

int sendVehicleTexture(int socket, Image *texture, int id) {
  char buf_send[BUFFERSIZE];
  ImagePacket *request = (ImagePacket *)malloc(sizeof(ImagePacket));
  PacketHeader ph;
  ph.type = PostTexture;
  request->header = ph;
  request->id = id;
  request->image = texture;

  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1)
    return -1;
  int bytes_sent = 0;
  int ret = 0;
  while (bytes_sent < size) {
    ret = send(socket, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(ret, "Can't send vehicle texture");
    if (ret == 0)
      break;
    bytes_sent += ret;
  }
  printf("[Vehicle texture] Sent bytes %d  \n", bytes_sent);
  return 0;
}

Image *getElevationMap(int socket) {
  char buf_send[BUFFERSIZE];
  char buf_rcv[BUFFERSIZE];
  ImagePacket *request = (ImagePacket *)malloc(sizeof(ImagePacket));
  PacketHeader ph;
  ph.type = GetElevation;
  request->header = ph;
  request->id = -1;
  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1)
    return NULL;
  int bytes_sent = 0;
  int ret = 0;

  while (bytes_sent < size) {
    ret = send(socket, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(ret, "Can't send Elevation Map request");
    if (ret == 0)
      break;
    bytes_sent += ret;
  }

  printf("[Elevation request] Sent %d bytes \n", bytes_sent);
  int msg_len = 0;
  int ph_len = sizeof(PacketHeader);
  while (msg_len < ph_len) {
    ret = recv(socket, buf_rcv, ph_len, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }

  PacketHeader *incoming_pckt = (PacketHeader *)buf_rcv;
  size = incoming_pckt->size - ph_len;
  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }

  ImagePacket *deserialized_packet =
      (ImagePacket *)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[Elevation request] Received %d bytes \n", msg_len + ph_len);
  Packet_free(&(request->header));
  Image *res = deserialized_packet->image;
  free(deserialized_packet);
  return res;
}

Image *getTextureMap(int socket) {
  char buf_send[BUFFERSIZE];
  char buf_rcv[BUFFERSIZE];
  ImagePacket *request = (ImagePacket *)malloc(sizeof(ImagePacket));
  PacketHeader ph;
  ph.type = GetTexture;
  request->header = ph;
  request->id = -1;
  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1)
    return NULL;
  int bytes_sent = 0;
  int ret = 0;
  while (bytes_sent < size) {
    ret = send(socket, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(ret, "Send Error");
    if (ret == 0)
      break;
    bytes_sent += ret;
  }
  printf("[Texture Map Request] Inviati %d bytes \n", bytes_sent);
  int msg_len = 0;
  int ph_len = sizeof(PacketHeader);
  while (msg_len < ph_len) {
    ret = recv(socket, buf_rcv, ph_len, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }
  PacketHeader *incoming_pckt = (PacketHeader *)buf_rcv;
  size = incoming_pckt->size - ph_len;
  printf("[Texture Request] Size da leggere %d \n", size);
  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR)
      continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }
  ImagePacket *deserialized_packet =
      (ImagePacket *)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[Texture Request] Ricevuto bytes %d \n", msg_len + ph_len);
  Packet_free(&(request->header));
  Image *res = deserialized_packet->image;
  free(deserialized_packet);
  return res;
}

int sendUpdates(int socket_udp, struct sockaddr_in server_addr, int serverlen) {
  char buf_send[BUFFERSIZE];
  PacketHeader ph;
  ph.type = VehicleUpdate;
  VehicleUpdatePacket* vup =
      (VehicleUpdatePacket*)malloc(sizeof(VehicleUpdatePacket));
  vup->header = ph;
  Vehicle_getForcesIntention(vehicle, &(vup->translational_force),
                             &(vup->rotational_force));
  Vehicle_setForcesIntention(vehicle, 0, 0);
  vup->id = id;
  int size = Packet_serialize(buf_send, &vup->header);
  int bytes_sent =
      sendto(socket_udp, buf_send, size, 0,
             (const struct sockaddr*)&server_addr, (socklen_t)serverlen);
  printf(
      "[UDP_Sender] Sent a VehicleUpdatePacket of %d bytes with tf:%f rf:%f \n",
      bytes_sent, vup->translational_force, vup->rotational_force);
  Packet_free(&(vup->header));
  struct timeval current_time;
  gettimeofday(&current_time, NULL);
  pthread_mutex_lock(&time_lock);
  if (last_update_time.tv_sec == -1) offline_server_counter++;
  if (offline_server_counter >= MAX_FAILED_ATTEMPTS &&
      current_time.tv_sec - start_time.tv_sec > MAX_TIME_WITHOUT_WORLDUPDATE) {
    connectivity = 0;
    exchange_update = 0;
    fprintf(stderr,
            "[WARNING] Server is not avaiable. Terminating the client now...");
    WorldViewer_exit(0);
  } else if (last_update_time.tv_sec != -1 &&
             current_time.tv_sec - last_update_time.tv_sec >
                 MAX_TIME_WITHOUT_WORLDUPDATE) {
    connectivity = 0;
    exchange_update = 0;
    fprintf(stderr,
            "[WARNING] Server is not avaiable. Terminating the client now...");
    WorldViewer_exit(0);
  } else if (last_update_time.tv_sec != -1)
    offline_server_counter = 0;
  pthread_mutex_unlock(&time_lock);
  if (bytes_sent < 0) return -1;
  return 0;
}

// Mark - TCP/UDP Handler

void *UDPSender(void *args) {
  udp_args argsUDP = *(udp_args *)args;
  struct sockaddr_in server_addr = argsUDP.server_addr;
  int socket_udp = argsUDP.socket_udp;
  int serverlen = sizeof(server_addr);
  while (connectivity && exchange_update) {
    int ret = sendUpdates(socket_udp, server_addr, serverlen);
    if (ret == -1)
      debug_print("[UDP_Sender] Cannot send VehicleUpdatePacket \n");
    usleep(SENDER_SLEEP);
  }
  pthread_exit(NULL);
}

// Receive and apply WorldUpdatePacket from server
void *UDPReceiver(void *args) {
  udp_args argsUDP = *(udp_args *)args;
  struct sockaddr_in server_addr = argsUDP.server_addr;
  int socket_udp = argsUDP.socket_udp;
  socklen_t addrlen = sizeof(server_addr);
  int socket_tcp = argsUDP.socket_tcp;
  while (connectivity && exchange_update) {
    char buf_rcv[BUFFERSIZE];
    int bytes_read = recvfrom(socket_udp, buf_rcv, BUFFERSIZE, 0,
                              (struct sockaddr *)&server_addr, &addrlen);
    if (bytes_read == -1) {
      printf("[UDP_Receiver] Can't receive Packet over UDP \n");
      usleep(RECEIVER_SLEEP);
      continue;
    }
    if (bytes_read == 0) {
      usleep(RECEIVER_SLEEP);
      continue;
    }

    printf("[UDP_Receiver] Received %d bytes over UDP\n", bytes_read);
    PacketHeader *ph = (PacketHeader *)buf_rcv;
    if (ph->size != bytes_read) {
      printf("[WARNING] Skipping partial UDP packet \n");
      usleep(RECEIVER_SLEEP);
      continue;
    }
    if (ph->type == WorldUpdate) {
      WorldUpdatePacket *wup =
          (WorldUpdatePacket *)Packet_deserialize(buf_rcv, bytes_read);
      pthread_mutex_lock(&time_lock);
      if (last_update_time.tv_sec != -1 &&
          timercmp(&last_update_time, &wup->time, >=)) {
        pthread_mutex_unlock(&time_lock);
        debug_print("[INFO] Ignoring a WorldUpdatePacket... \n");
        Packet_free(&wup->header);
        usleep(RECEIVER_SLEEP);
        continue;
      }
      printf("WorldUpdatePacket contains %d vehicles besides mine \n",
             wup->num_vehicles - 1);
      last_update_time = wup->time;
      pthread_mutex_unlock(&time_lock);
      char mask[WORLDSIZE];
      for (int k = 0; k < WORLDSIZE; k++)
        mask[k] = UNTOUCHED;
      for (int i = 0; i < wup->num_vehicles; i++) {
        int new_position = -1;
        int id_struct = addUser(lw->ids, WORLDSIZE, wup->updates[i].id,
                                &new_position, &(lw->users_online));
        if (wup->updates[i].id == id) {
          pthread_mutex_lock(&lw->vehicles[0]->mutex);
          Vehicle_setXYTheta(lw->vehicles[0], wup->updates[i].x,
                             wup->updates[i].y, wup->updates[i].theta);
          Vehicle_setForcesUpdate(lw->vehicles[0],
                                  wup->updates[i].translational_force,
                                  wup->updates[i].rotational_force);
          World_manualUpdate(&world, lw->vehicles[0],
                             wup->updates[i].client_update_time);
          pthread_mutex_unlock(&lw->vehicles[0]->mutex);
        } else if (id_struct == -1) {
          if (new_position == -1)
            continue;
          mask[new_position] = TOUCHED;
          debug_print("New Vehicle with id %d and x: %f y: %f z: %f \n",
                      wup->updates[i].id, wup->updates[i].x, wup->updates[i].y,
                      wup->updates[i].theta);
          Image *img = getVehicleTexture(socket_tcp, wup->updates[i].id);
          if (img == NULL)
            continue;
          Vehicle *new_vehicle = (Vehicle *)malloc(sizeof(Vehicle));
          Vehicle_init(new_vehicle, &world, wup->updates[i].id, img);
          lw->vehicles[new_position] = new_vehicle;
          pthread_mutex_lock(&lw->vehicles[new_position]->mutex);
          Vehicle_setXYTheta(lw->vehicles[new_position], wup->updates[i].x,
                             wup->updates[i].y, wup->updates[i].theta);
          Vehicle_setForcesUpdate(lw->vehicles[new_position],
                                  wup->updates[i].translational_force,
                                  wup->updates[i].rotational_force);
          pthread_mutex_unlock(&lw->vehicles[new_position]->mutex);
          World_addVehicle(&world, new_vehicle);
          lw->has_vehicle[new_position] = 1;
          lw->vehicle_login_time[new_position] =
              wup->updates[i].client_creation_time;
        } else {
          mask[id_struct] = TOUCHED;
          if (timercmp(&wup->updates[i].client_creation_time,
                       &lw->vehicle_login_time[id_struct], !=)) {
            debug_print("[WARNING] Forcing refresh for client with id %d",
                        wup->updates[i].id);
            if (lw->has_vehicle[id_struct]) {
              Image *im = lw->vehicles[id_struct]->texture;
              World_detachVehicle(&world, lw->vehicles[id_struct]);
              Vehicle_destroy(lw->vehicles[id_struct]);
              if (im != NULL)
                Image_free(im);
              free(lw->vehicles[id_struct]);
            }
            Image *img = getVehicleTexture(socket_tcp, wup->updates[i].id);
            if (img == NULL)
              continue;
            Vehicle *new_vehicle = (Vehicle *)malloc(sizeof(Vehicle));
            Vehicle_init(new_vehicle, &world, wup->updates[i].id, img);
            lw->vehicles[id_struct] = new_vehicle;
            pthread_mutex_lock(&lw->vehicles[id_struct]->mutex);
            Vehicle_setXYTheta(lw->vehicles[id_struct], wup->updates[i].x,
                               wup->updates[i].y, wup->updates[i].theta);
            Vehicle_setForcesUpdate(lw->vehicles[id_struct],
                                    wup->updates[i].translational_force,
                                    wup->updates[i].rotational_force);
            World_manualUpdate(&world, lw->vehicles[id_struct],
                               wup->updates[i].client_update_time);
            pthread_mutex_unlock(&lw->vehicles[id_struct]->mutex);
            World_addVehicle(&world, new_vehicle);
            lw->has_vehicle[id_struct] = 1;
            lw->vehicle_login_time[id_struct] =
                wup->updates[i].client_creation_time;
            continue;
          }
          debug_print("Updating Vehicle with id %d and x: %f y: %f z: %f \n",
                      wup->updates[i].id, wup->updates[i].x, wup->updates[i].y,
                      wup->updates[i].theta);
          pthread_mutex_lock(&lw->vehicles[id_struct]->mutex);
          Vehicle_setXYTheta(lw->vehicles[id_struct], wup->updates[i].x,
                             wup->updates[i].y, wup->updates[i].theta);
          Vehicle_setForcesUpdate(lw->vehicles[id_struct],
                                  wup->updates[i].translational_force,
                                  wup->updates[i].rotational_force);
          World_manualUpdate(&world, lw->vehicles[id_struct],
                             wup->updates[i].client_update_time);
          pthread_mutex_unlock(&lw->vehicles[id_struct]->mutex);
        }
      }
      for (int i = 0; i < WORLDSIZE; i++) {
        if (mask[i] == TOUCHED)
          continue;
        if (i == 0)
          continue;
        if (mask[i] == UNTOUCHED && lw->ids[i] != -1) {
          debug_print("[WorldUpdate] Removing Vehicles with ID %d \n",
                      lw->ids[i]);
          lw->users_online = lw->users_online - 1;
          if (!lw->has_vehicle[i])
            continue;
          Image *im = lw->vehicles[i]->texture;
          World_detachVehicle(&world, lw->vehicles[i]);
          if (im != NULL)
            Image_free(im);
          Vehicle_destroy(lw->vehicles[i]);
          lw->ids[i] = -1;
          free(lw->vehicles[i]);
          lw->has_vehicle[i] = 0;
        }
      }
      Packet_free(&wup->header);
      break;
    }
    usleep(RECEIVER_SLEEP);
  }
  pthread_exit(NULL);
}

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
  int reuseaddr_opt = 1; // recover server if a crash occurs
  int ret = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt,
                       sizeof(reuseaddr_opt));
  ERROR_HELPER(ret, "Can't set SO_REUSEADDR flag");

  ret = connect(socket_desc, (struct sockaddr *)&server_addr,
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
  Image *surface_elevation = getElevationMap(socket_desc);
  fprintf(stdout, "[Main] Map elevation received \n");

  // Surface Eleveation
  Image *surface_texture = getTextureMap(socket_desc);
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

  uint16_t port_number_udp = htons((uint16_t)8888); // we use network byte order
  socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(socket_desc, "Can't create an UDP socket");
  struct sockaddr_in udp_server = {0};
  udp_server.sin_addr.s_addr = ip_addr;
  udp_server.sin_family = AF_INET;
  udp_server.sin_port = port_number_udp;
  printf("[Main] Socket UDP created and ready to work \n");

  // Create UDP Threads
  pthread_t UDP_sender_thread, UDP_receiver_thread;
  udp_args argsUDP;
  argsUDP.socket_tcp = socket_desc;
  argsUDP.server_addr = udp_server;
  argsUDP.socket_udp = socket_udp;

  ret = pthread_create(&UDP_sender_thread, NULL, UDPSender, &argsUDP);
  PTHREAD_ERROR_HELPER(ret, "[MAIN] pthread_create on thread UDP_sender");
  ret = pthread_create(&UDP_receiver_thread, NULL, UDPReceiver, &argsUDP);
  PTHREAD_ERROR_HELPER(ret, "[MAIN] pthread_create on thread UDP_receiver");

  WorldViewer_runGlobal(&world, vehicle, &argc, argv);

  // cleanup
  World_destroy(&world);
  Image_free(surface_elevation);
  Image_free(surface_texture);
  Image_free(my_texture);
  exit(0);
}
