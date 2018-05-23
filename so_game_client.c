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
#include <unistd.h>

#include "common.h"
#include "image.h"
#include "semaphore.h"
#include "so_game_protocol.h"
#include "surface.h"
#include "user_list.h"
#include "vehicle.h"
#include "world.h"
#include "world_viewer.h"

// Mark - Structs and Variables

typedef struct localWorld {
    int id_list[WORLD_SIZE];
    int players_online;
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
    int my_id;
    int tcp_socket;
    int udp_socket;
void* UDPSender(void* args)
{
    int ret;
    char buffer[BUFFER_SIZE];

    printf("[UDP SENDER] Sender thread started...\n");

    // Preparazione connessione
    udp_args_t* udp_args = (udp_args_t*)args;
    int udp_socket = udp_args->udp_socket;
    struct sockaddr_in server_addr = udp_args->server_addr;
    int sockaddr_len = sizeof(server_addr);

    while (1) {
        VehicleUpdatePacket* vehicle_packet =
            (VehicleUpdatePacket*)malloc(sizeof(VehicleUpdatePacket));
        PacketHeader header;
        header.type = VehicleUpdate;

        // Contenuto del pacchetto
        vehicle_packet->header = header;
        vehicle_packet->rotational_force = vehicle->rotational_force_update;
        vehicle_packet->translational_force =
            vehicle->translational_force_update;

        // Serializzazione del pacchetto
        int buffer_size = Packet_serialize(buffer, &vehicle_packet->header);

        // Invio il pacchetto
        ret = sendto(udp_socket, buffer, buffer_size, 0,
                     (const struct sockaddr*)&server_addr, (socklen_t)sockaddr_len);
        ERROR_HELPER(ret, "[ERROR] Failed sending update to the server!!!");

        usleep(TIME_TO_SLEEP);
    }

    printf("[UDP SENDER] Closed sender...\n");

    pthread_exit(0);
}

void* UDPReceiver(void* args)
{
    int ret;
    int buffer_size = 0;
    char buffer[BUFFER_SIZE];

    printf("[UDP RECEIVER] Receiving updates...\n");

    // Preparazione connessione
    udp_args_t* udp_args = (udp_args_t*)args;
    int udp_socket = udp_args->udp_socket;

    struct sockaddr_in server_addr = udp_args->server_addr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    while ((ret = recvfrom(udp_socket, buffer, BUFFER_SIZE, 0,
                           (struct sockaddr*)&server_addr, &addrlen)) > 0) {
        ERROR_HELPER(ret, "[ERROR] Cannot receive packet from server!!!");

        buffer_size += ret;

        WorldUpdatePacket* world_update =
            (WorldUpdatePacket*)Packet_deserialize(buffer, buffer_size);

        // Aggiorna le posizioni dei veicoli

        for (int i = 0; i < world_update->num_vehicles; i++) {
            ClientUpdate* client = &(world_update->updates[i]);

            Vehicle* client_vehicle = World_getVehicle(&world, client->id);

            printf("[UDP RECEIVER] Vehicle id (%d)...\n", client_vehicle->id);

            if (client_vehicle == 0) {
                Vehicle* v = (Vehicle*)malloc(sizeof(Vehicle));
                Vehicle_init(v, &world, client->id, vehicle->texture);
                // printf("[MAIN] Vehicle initialized...\n");
                World_addVehicle(&world, v);
            }

            client_vehicle = World_getVehicle(&world, client->id);

            client_vehicle->x = client->x;
            client_vehicle->y = client->y;
            client_vehicle->theta = client->theta;
        }

        World_update(&world);
    }

    printf("[UDP RECEIVER] Closed receiver...\n");
    pthread_exit(0);
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
  //debug_print("[Vehicle texture] Sent bytes %d  \n", bytes_sent);
  return 0;
}


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
    printf("[MAIN] Vehicle added...\n");

    // spawn a thread that will listen the update messages from
    // the server, and sends back the controls
    // the update for yourself are written in the desired_*_force
    // fields of the vehicle variable
    // when the server notifies a new player has joined the game
    // request the texture and add the player to the pool

    
    pthread_t UDPSender_thread, UDPReceiver_thread;
    udp_args_t udp_args;
    udp_args.server_addr = udp_server;
    udp_args.udp_socket = udp_socket;
    udp_args.tcp_socket = tcp_socket;

      // Threads
  ret = pthread_create(&UDPSender_thread, NULL, UDPSender, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create UDPsender");

  ret = pthread_create(&UDPReceiver_thread, NULL, UDPReceiver, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create UDP receiver");

    WorldViewer_runGlobal(&world, vehicle, &argc, argv);


    // printf("[MAIN] Thrads joined...\n");
    // printf("[MAIN] Closing...\n");

    // cleanup
    World_destroy(&world);
    return 0;
}
