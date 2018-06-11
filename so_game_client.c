#include <GL/glut.h>
#include <arpa/inet.h> 
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h> 
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "common.h"
#include "image.h"
#include "semaphore.h"
#include "so_game_protocol.h"
#include "surface.h"
#include "vehicle.h"
#include "world.h"
#include "world_viewer.h"

// Mark - Structs and Variables

typedef struct localWorld {
    int ids[WORLDSIZE];
    char has_vehicle[WORLDSIZE];
    int users_online;
    struct timeval vehicle_login_time[WORLDSIZE];
    Vehicle** vehicles;
} localWorld;

typedef struct {
    localWorld* lw;
    struct sockaddr_in server_addr;
    int tcp_socket;
    int udp_socket;
} udp_args_t;

typedef struct {
    volatile int run;
    World* world;
} UpdaterArgs;

int window;
WorldViewer viewer;
World world;
Vehicle* vehicle;
char connectivity = 1;
int my_id;
int tcp_socket;
int udp_socket;
pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;

int addUser(int ids[], int size, int id2, int* position, int* users_online) {
    if (*users_online == WORLDSIZE) {
        *position = -1;
        return -1;
    }
    for (int i = 0; i < size; i++) {
        if (ids[i] == id2) {
            return i;
        }
    }
    for (int i = 0; i < size; i++) {
        if (ids[i] == -1) {
            ids[i] = id2;
            *users_online += 1;
            *position = i;
            break;
        }
    }
    return -1;
}

Image* getVehicleTexture(int socket, int id) {
  char buf_send[BUFFER_SIZE];
  char buf_rcv[BUFFER_SIZE];
  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));
  PacketHeader ph;
  ph.type = GetTexture;
  request->header = ph;
  request->id = id;
  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1) return NULL;
  int bytes_sent = 0;
  int ret = 0;
  while (bytes_sent < size) {
    ret = send(socket, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Can't request a texture of a vehicle");
    if (ret == 0) break;
    bytes_sent += ret;
  }
  Packet_free(&(request->header));

  int ph_len = sizeof(PacketHeader);
  int msg_len = 0;
  while (msg_len < ph_len) {
    ret = recv(socket, buf_rcv + msg_len, ph_len - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(msg_len, "Cannot read from socket");
    msg_len += ret;
  }
  PacketHeader* header = (PacketHeader*)buf_rcv;
  size = header->size - ph_len;
  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(msg_len, "Cannot read from socket");
    msg_len += ret;
  }
  ImagePacket* deserialized_packet =
      (ImagePacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[Get Vehicle Texture] Received %d bytes \n", msg_len + ph_len);
  Image* im = deserialized_packet->image;
  free(deserialized_packet);
  return im;
}

int sendUpdates(int socket_udp, struct sockaddr_in server_addr, int serverlen) {
  char buf_send[BUFFER_SIZE];
  PacketHeader ph;
  ph.type = VehicleUpdate;
  VehicleUpdatePacket* vup =
      (VehicleUpdatePacket*)malloc(sizeof(VehicleUpdatePacket));
  vup->header = ph;
  pthread_mutex_lock(&vehicle->mutex);
  Vehicle_getForcesIntentionUpdate(vehicle, &(vup->translational_force),
                             &(vup->rotational_force));
  Vehicle_setForcesIntention(vehicle, 0, 0);
  pthread_mutex_unlock(&vehicle->mutex);
  vup->id = my_id;
  int size = Packet_serialize(buf_send, &vup->header);
  int bytes_sent =
      sendto(socket_udp, buf_send, size, 0,
             (const struct sockaddr*)&server_addr, (socklen_t)serverlen);
  printf(
      "[UDP_Sender] Sent a VehicleUpdatePacket of %d bytes with tf:%f rf:%f \n",
      bytes_sent, vup->translational_force, vup->rotational_force);
  Packet_free(&(vup->header));
  if (bytes_sent < 0) return -1;
  return 0;
}

void* UDPSender(void* args) {
  udp_args_t udp_args = *(udp_args_t*)args;
  struct sockaddr_in server_addr = udp_args.server_addr;
  int socket_udp = udp_args.udp_socket;
  int serverlen = sizeof(server_addr);
  while (connectivity) {
    int ret = sendUpdates(socket_udp, server_addr, serverlen);
    if (ret == -1)
      printf("[UDP_Sender] Cannot send VehicleUpdatePacket \n");
    usleep(SENDER_SLEEP_C);
  }
  pthread_exit(NULL);
}

void* UDPReceiver(void* args) {
    udp_args_t udp_args = *(udp_args_t*)args;
    struct sockaddr_in server_addr = udp_args.server_addr;
    int socket_udp = udp_args.udp_socket;
    int socket_tcp = udp_args.tcp_socket;
    socklen_t addrlen = sizeof(server_addr);
    localWorld* lw = udp_args.lw;
    while (connectivity) {
        char buf_rcv[BUFFER_SIZE];
        int bytes_read = recvfrom(socket_udp, buf_rcv, BUFFER_SIZE, 0,
                                  (struct sockaddr*)&server_addr, &addrlen);
        if (bytes_read == -1) {
            printf("[UDP_Receiver] Can't receive Packet over UDP \n");
            usleep(RECEIVER_SLEEP_C);
            continue;
        }
        if (bytes_read == 0) {
            usleep(RECEIVER_SLEEP_C);
            continue;
        }
 
        printf("[UDP_Receiver] Received %d bytes over UDP\n", bytes_read);
        PacketHeader* ph = (PacketHeader*)buf_rcv;
        if (ph->size != bytes_read) {
            printf("[WARNING] Skipping partial UDP packet \n");
            usleep(RECEIVER_SLEEP_C);
            continue;
        }
        
        WorldUpdatePacket* wup =
            (WorldUpdatePacket*)Packet_deserialize(buf_rcv, bytes_read);
        pthread_mutex_lock(&time_lock);

        printf("WorldUpdatePacket contains %d vehicles besides mine \n",
                    wup->num_vehicles - 1);;
        pthread_mutex_unlock(&time_lock);
        char mask[WORLDSIZE];
        for (int k = 0; k < WORLDSIZE; k++) mask[k] = UNTOUCHED;
        for (int i = 0; i < wup->num_vehicles; i++) {
            int new_position = -1;
            int id_struct = addUser(lw->ids, WORLDSIZE, wup->updates[i].id,
                                    &new_position, &(lw->users_online));
            if (wup->updates[i].id == my_id) {
                pthread_mutex_lock(&lw->vehicles[0]->mutex);
                Vehicle_setXYTheta(lw->vehicles[0], wup->updates[i].x,
                                   wup->updates[i].y, wup->updates[i].theta);
                Vehicle_setForcesUpdate(lw->vehicles[0], wup->updates[i].translational_force, wup->updates[i].rotational_force);
                pthread_mutex_unlock(&lw->vehicles[0]->mutex);
            } else if (id_struct == -1) {
                if (new_position == -1) continue;
                mask[new_position] = TOUCHED;
                printf("New Vehicle with id %d and x: %f y: %f z: %f \n",
                            wup->updates[i].id, wup->updates[i].x,
                            wup->updates[i].y, wup->updates[i].theta);
                Image* img = getVehicleTexture(socket_tcp, wup->updates[i].id);
                if (img == NULL) continue;
                Vehicle* new_vehicle = (Vehicle*)malloc(sizeof(Vehicle));
                Vehicle_init(new_vehicle, &world, wup->updates[i].id, img);
                lw->vehicles[new_position] = new_vehicle;
                pthread_mutex_lock(&lw->vehicles[new_position]->mutex);
                Vehicle_setXYTheta(lw->vehicles[new_position],
                                   wup->updates[i].x, wup->updates[i].y,
                                   wup->updates[i].theta);
                Vehicle_setForcesUpdate(lw->vehicles[0], wup->updates[i].translational_force, wup->updates[i].rotational_force);
                pthread_mutex_unlock(&lw->vehicles[new_position]->mutex);
                World_addVehicle(&world, new_vehicle);
                lw->has_vehicle[new_position] = 1;
            } else {
                mask[id_struct] = TOUCHED;
                if (timercmp(&wup->updates[i].client_creation_time,
                             &lw->vehicle_login_time[id_struct], !=)) {
                    printf(
                        "[WARNING] Forcing refresh for client with id %d",
                        wup->updates[i].id);
                    if (lw->has_vehicle[id_struct]) {
                        Image* im = lw->vehicles[id_struct]->texture;
                        World_detachVehicle(&world, lw->vehicles[id_struct]);
                        Vehicle_destroy(lw->vehicles[id_struct]);
                        if (im != NULL) Image_free(im);
                        free(lw->vehicles[id_struct]);
                    }
                    Image* img =
                        getVehicleTexture(socket_tcp, wup->updates[i].id);
                    if (img == NULL) continue;
                    Vehicle* new_vehicle = (Vehicle*)malloc(sizeof(Vehicle));
                    Vehicle_init(new_vehicle, &world, wup->updates[i].id, img);
                    lw->vehicles[id_struct] = new_vehicle;
                    pthread_mutex_lock(&lw->vehicles[id_struct]->mutex);
                    Vehicle_setXYTheta(lw->vehicles[id_struct],
                                       wup->updates[i].x, wup->updates[i].y,
                                       wup->updates[i].theta);
                    pthread_mutex_unlock(&lw->vehicles[id_struct]->mutex);
                    printf("[UDP_Receiver] Updateded 3 %f %f %f \n", wup->updates[i].x,
                                   wup->updates[i].y, wup->updates[i].theta);
                    World_addVehicle(&world, new_vehicle);
                    lw->has_vehicle[id_struct] = 1;
                    lw->vehicle_login_time[id_struct] =
                        wup->updates[i].client_creation_time;
                    continue;
                }
                printf(
                    "Updating Vehicle with id %d and x: %f y: %f z: %f \n",
                    wup->updates[i].id, wup->updates[i].x, wup->updates[i].y,
                    wup->updates[i].theta);
                pthread_mutex_lock(&lw->vehicles[id_struct]->mutex);
                Vehicle_setXYTheta(lw->vehicles[id_struct], wup->updates[i].x,
                                   wup->updates[i].y, wup->updates[i].theta);
                pthread_mutex_unlock(&lw->vehicles[id_struct]->mutex);
            }
        }
        for (int i = 0; i < WORLDSIZE; i++) {
            if (mask[i] == TOUCHED) continue;
            if (i == 0) continue;
 
            if (lw->ids[i] == my_id) continue;
            if (mask[i] == UNTOUCHED && lw->ids[i] != -1) {
                printf("[WorldUpdate] Removing Vehicles with ID %d \n",
                            lw->ids[i]);
                lw->users_online = lw->users_online - 1;
                if (!lw->has_vehicle[i]) continue;
                Image* im = lw->vehicles[i]->texture;
                World_detachVehicle(&world, lw->vehicles[i]);
                if (im != NULL) Image_free(im);
                Vehicle_destroy(lw->vehicles[i]);
                lw->ids[i] = -1;
                free(lw->vehicles[i]);
                lw->has_vehicle[i] = 0;
            }
 
            Packet_free(&wup->header);
            break;
        }
        usleep(RECEIVER_SLEEP_C);
    }
    pthread_exit(NULL);
}

int getID(int socket_desc) {
  char buf_send[BUFFER_SIZE];
  char buf_rcv[BUFFER_SIZE];
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
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }
  PacketHeader* header = (PacketHeader*)buf_rcv;
  size = header->size - ph_len;

  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket_desc, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }
  IdPacket* deserialized_packet =
      (IdPacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
  //debug_print("[Get Id] Received %dbytes \n", msg_len + ph_len);
  int id = deserialized_packet->id;
  Packet_free(&(deserialized_packet->header));
  return id;
}

Image* getTextureMap(int socket) {
  char buf_send[BUFFER_SIZE];
  char buf_rcv[BUFFER_SIZE];
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
    ERROR_HELPER(ret, "Errore invio");
    if (ret == 0) break;
    bytes_sent += ret;
  }
  printf("[Texture request] Inviati %d bytes \n", bytes_sent);
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
  printf("[Texture Request] Size da leggere %d - ph len %d \n", size,ph_len);
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
  Image* ris = deserialized_packet->image;
  free(deserialized_packet);
  return ris;
}

Image* getElevation(int socket) {
  char buf_send[BUFFER_SIZE];
  char buf_rcv[BUFFER_SIZE];
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

  //debug_print("[Elevation request] Sent %d bytes \n", bytes_sent);
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
  //debug_print("[Elevation request] Received %d bytes \n", msg_len + ph_len);
  Packet_free(&(request->header));
  Image* ris = deserialized_packet->image;
  free(deserialized_packet);
  return ris;
}

int sendVehicleTexture(int socket, Image* texture, int id) {
  char buf_send[BUFFER_SIZE];
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
  return 0;
}

// Mark - Main

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("usage: %s <server_address> <player texture>\n", argv[1]);
        exit(-1);
    }

    printf("loading texture image from %s ... ", argv[1]);
    Image* my_texture = Image_load(argv[1]);
    if (my_texture) {
        printf("Done! \n");
    } else {
        printf("Fail! \n");
    }

    int ret;
    localWorld* local_world = malloc(sizeof(localWorld));
    local_world->vehicles = malloc(sizeof(Vehicle*) * WORLDSIZE);

    for(int i = 0; i < WORLDSIZE; i++){
      local_world->ids[i] = -1;
      local_world->has_vehicle[i] = 0;
    }

    // Apertura connessione TCP
    struct sockaddr_in server_addr = {0};
    uint16_t port_number_no = htons((uint16_t)TCP_PORT);
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    in_addr_t ip_addr = inet_addr(SERVER_ADDRESS);
    ERROR_HELPER(tcp_socket, "Cannot create socket \n");
    server_addr.sin_addr.s_addr = ip_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = port_number_no;

    int reuseaddr_opt = 1;  // recover server if a crash occurs
    ret = setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt,
                       sizeof(reuseaddr_opt));
    ERROR_HELPER(ret, "Can't set SO_REUSEADDR flag");

    ret = connect(tcp_socket, (struct sockaddr*)&server_addr,
                sizeof(struct sockaddr_in));
    ERROR_HELPER(ret, "Cannot connect to remote server \n");
    printf("[MAIN] Connection enstablished with server...\n");

    // Apertura connessione UDP
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    ERROR_HELPER(udp_socket, "[ERROR] Can't create an UDP socket");
    struct sockaddr_in udp_server = { 0 };
    udp_server.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
    udp_server.sin_family = AF_INET;
    udp_server.sin_port = htons(UDP_PORT);

    // Scambia messaggi TCP
    my_id = getID(tcp_socket);
    local_world->ids[0] = my_id;
    printf("ID Ricevuto %d \n",my_id);

    Image* map_texture = getTextureMap(tcp_socket);
    printf("Texture ricevuta \n");

    Image* map_elevation = getElevation(tcp_socket);
    printf("Elevation ricevuta \n");

    sendVehicleTexture(tcp_socket, my_texture, my_id);
    printf("Texture inviata \n");

    World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
    printf("[MAIN] World initialized...\n");

    vehicle = (Vehicle*)malloc(sizeof(Vehicle));
    Vehicle_init(vehicle, &world, my_id, my_texture);
    printf("[MAIN] Vehicle initialized...\n");

    World_addVehicle(&world, vehicle);
    local_world->vehicles[0] = vehicle;
    local_world->has_vehicle[0] = 1;
    printf("[MAIN] Vehicle added...\n");

    pthread_t UDPSender_thread, UDPReceiver_thread;
    udp_args_t udp_args;
    udp_args.lw = local_world;
    udp_args.server_addr = udp_server;
    udp_args.udp_socket = udp_socket;
    udp_args.tcp_socket = tcp_socket;

    // Threads
    ret = pthread_create(&UDPSender_thread, NULL, UDPSender, &udp_args);
    PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create UDPsender");

    ret = pthread_create(&UDPReceiver_thread, NULL, UDPReceiver, &udp_args);
    PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create UDP receiver");

    WorldViewer_runGlobal(&world, vehicle, &argc, argv);

    // Join dei thread
    ret = pthread_join(UDPReceiver_thread, NULL);
    ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join World Updatethread");

    ret = pthread_join(UDPSender_thread, NULL);
    ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join TCP server connection thread");

    // cleanup
    World_destroy(&world);
    return 0;
}
