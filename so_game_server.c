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
#include "so_game_protocol.h"
#include "surface.h"
#include "user_list.h"
#include "vehicle.h"
#include "world.h"
#include "world_viewer.h"

// Mark - Structs and Variables

typedef struct {
  int client_desc;
  struct sockaddr_in client_addr;
  Image* elevation_texture;
  Image* surface_elevation;
  int tcp_socket;
} tcp_args_t;

typedef struct {
  int udp_socket;
} udp_args_t;

World world;
UserHead* users;

// Mark - Network

int TCPCore(int tcp_socket, int id, char* buffer, Image* surface_elevation, Image* elevation_texture, int len, User* user)
{
  PacketHeader* header = (PacketHeader*)buffer;
  if (header->type == GetId) {
    printf("[TCP] ID requested from (%d)...\n", id);
    IdPacket* id_to_send = (IdPacket*)malloc(sizeof(IdPacket));

    PacketHeader header;
    header.type = GetId;
    id_to_send->header = header;
    id_to_send->id = id;  

    char buffer_send[BUFFER_SIZE];
    int pckt_length = Packet_serialize(
        buffer_send,
        &(id_to_send->header));
    int bytes_sent = 0;
    int ret;
    while (bytes_sent < pckt_length) {
      ret = send(tcp_socket, buffer_send + bytes_sent, pckt_length - bytes_sent,
                 0);
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "[ERROR] Error assigning ID!!!");
      if (ret == 0) break;
      bytes_sent += ret;
    }

    printf("[TCP] ID sent to (%d)...\n", id);  // DEBUG OUTPUT

    return 1;
  } else if (header->type == GetTexture) {
    printf("[TCP] Texture requested from (%d)...\n", id);

    // Converto il pacchetto ricevuto in un ImagePacket per estrarne la texture
    // richiesta
    ImagePacket* texture_request = (ImagePacket*)buffer;
    int id_request = texture_request->id;

    // Preparo header per la risposta
    PacketHeader header_send;
    header_send.type = PostTexture;

    // Preparo il pacchetto per inviare la texture al client
    ImagePacket* texture_to_send = (ImagePacket*)malloc(sizeof(ImagePacket));
    texture_to_send->header = header_send;
    texture_to_send->id = id_request;
    texture_to_send->image = elevation_texture;

    char buffer_send[BUFFER_SIZE];
    int pckt_length = Packet_serialize(
        buffer_send,
        &(texture_to_send->header));  // Ritorna il numero di bytes scritti

    // Invio del messaggio tramite socket
    int bytes_sent = 0;
    int ret;
    while (bytes_sent < pckt_length) {
      ret = send(tcp_socket, buffer_send + bytes_sent, pckt_length - bytes_sent,
                 0);
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "[ERROR] Error requesting texture!!!");
      if (ret == 0) break;
      bytes_sent += ret;
    }

    // Packet_free(&(texture_to_send->header));   // Libera la memoria del
    // pacchetto non più utilizzato free(texture_to_send);

    printf("[TCP] Texture sent to (%d)...\n", id);  // DEBUG OUTPUT

    return 1;
  } else if (header->type == GetElevation) {
    printf("[TCP] Elevation requested from (%d)...\n", id);

    // Converto il pacchetto ricevuto in un ImagePacket per estrarne la
    // elevation richiesta
    ImagePacket* elevation_request = (ImagePacket*)buffer;
    int id_request = elevation_request->id;

    // Preparo header per la risposta
    PacketHeader header_send;
    header_send.type = PostElevation;

    // Preparo il pacchetto per inviare la elevation al client
    ImagePacket* elevation_to_send = (ImagePacket*)malloc(sizeof(ImagePacket));
    elevation_to_send->header = header_send;
    elevation_to_send->id = id_request;
    elevation_to_send->image = surface_elevation;

    char buffer_send[BUFFER_SIZE];
    int pckt_length = Packet_serialize(
        buffer_send,
        &(elevation_to_send->header));  // Ritorna il numero di bytes scritti

    // Invio del messaggio tramite socket
    int bytes_sent = 0;
    int ret;
    while (bytes_sent < pckt_length) {
      ret = send(tcp_socket, buffer_send + bytes_sent, pckt_length - bytes_sent,
                 0);
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "[ERROR] Error requesting elevation texture!!!");
      if (ret == 0) break;
      bytes_sent += ret;
    }

    // Packet_free(&(elevation_to_send->header));   // Libera la memoria del
    // pacchetto non più utilizzato free(elevation_to_send);

    printf("[TCP] Elevation sent to (%d)...\n", id);  // DEBUG OUTPUT

    return 1;
  } else if (header->type == PostTexture) {
    int ret;

    if (len < header->size) {
      // printf("[TCP] Received packet vehicle (size = %d)...\n", len);
      return -1;
    }

    // Deserializzazione del pacchetto
    PacketHeader* received_header = Packet_deserialize(buffer, header->size);
    ImagePacket* received_texture = (ImagePacket*)received_header;

    printf("[TCP] Vehicle sent from (%d)...\n", id);

    // Aggiunta veicolo nuovo al mondo
    Vehicle* new_vehicle = malloc(sizeof(Vehicle));
    Vehicle_init(new_vehicle, &world, id, received_texture->image);
    World_addVehicle(&world, new_vehicle);

    // Rimanda la texture al client come conferma
    PacketHeader header_aux;
    header_aux.type = PostTexture;

    ImagePacket* texture_for_client = (ImagePacket*)malloc(sizeof(ImagePacket));
    texture_for_client->image = received_texture->image;
    texture_for_client->header = header_aux;

    // Serializza la texture da mandare
    int buffer_size = Packet_serialize(buffer, &texture_for_client->header);

    // Invia la texture
    while ((ret = send(tcp_socket, buffer, buffer_size, 0)) < 0) {
      if (errno == EINTR) continue;
      ERROR_HELPER(ret, "[ERROR] Cannot write to socket!!!");
    }

    printf("[TCP] Vehicle texture sent back to (%d)...\n", id);

    // Packet_free(&received_texture->header); // Libera la memoria del
    // pacchetto non più utilizzato Packet_free(&texture_for_client->header);
    User_insert_last(users, user);

    printf("[TCP] User (%d) inserted...\n", user->id);

    return 1;
  } else {
    printf("[ERROR] Unknown packet received from %d!!!\n", id);  // DEBUG OUTPUT
  }

  return -1;  // Return in caso di errore
}

void* TCPClientHandler(void* args)
{
  tcp_args_t* tcp_args = (tcp_args_t*)args;
  printf("[TCP Client Handler] Handling client with client descriptor (%d)\n", tcp_args->client_desc);
  int tcp_client_desc = tcp_args->client_desc;
  int msg_length = 0;
  int ret;
  char buffer_recv[BUFFER_SIZE];  // Conterrà il PacketHeader
  printf("[TCP Client Handler] Creating user with id: %d\n", tcp_client_desc);

  User* user = (User*)malloc(sizeof(User));
  user->id = tcp_client_desc;
  user->user_addr_tcp = tcp_args->client_addr;
  user->x = 0;
  user->y = 0;
  user->theta = 0;
  user->translational_force = 0;
  user->rotational_force = 0;
  user->vehicle = NULL;

  // Ricezione del pacchetto
  int packet_length = BUFFER_SIZE;
  while (1) {
    while ((ret = recv(tcp_client_desc, buffer_recv + msg_length,
                       packet_length - msg_length, 0)) < 0) {
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "[ERROR] Failed to receive packet!!!");
    }
    // Client disconnesso
    if (ret == 0) {
      printf("[TCP Client Handler] Connection closed with: %d\n", user->id);
      if (User_detach(users, user->id) == 1)
        printf("[TCP Client Handler] User %d removed.\n", user->id);

      break;
    }

    msg_length += ret;

    // printf("[TCP] Received packet (total size = %d)...\n", ((PacketHeader*)
    // buffer_recv)->size);

    // Gestione del pacchetto ricevuto tramite l'handler dei pacchetti
    ret = TCPCore(tcp_client_desc, tcp_args->client_desc, buffer_recv,
                     tcp_args->surface_elevation, tcp_args->elevation_texture,
                     msg_length, user);

    if (ret == 1) {
      // printf("[TCP] Success...\n");

      msg_length = 0;
      continue;
    } else {
      // printf("[TCP] Next packet...\n");
      continue;
    }
  }
  pthread_exit(0);
}

void* TCP_handler(void* args)
{
  printf("[TCP Handler] Started...\n");
  tcp_args_t* tcp_args = (tcp_args_t*)args;
  int ret;
  int tcp_client_desc;
  int tcp_socket = tcp_args->tcp_socket;

  printf("[TCP Handler] Accepting connection from clients...\n");

  int sockaddr_len = sizeof(struct sockaddr_in);
  struct sockaddr_in client_addr;

  while ((tcp_client_desc = accept(tcp_socket, (struct sockaddr*)&client_addr, (socklen_t*)&sockaddr_len)) > 0) {
    printf("[TCP Handler] Connection enstablished with (%d)...\n", tcp_client_desc);
    pthread_t client_thread;
    tcp_args_t tcp_args_aux;
    tcp_args_aux.client_desc = tcp_client_desc;
    tcp_args_aux.elevation_texture = tcp_args->elevation_texture;
    tcp_args_aux.surface_elevation = tcp_args->surface_elevation;
    tcp_args_aux.client_addr = client_addr;

    ret = pthread_create(&client_thread, NULL, TCPClientHandler, &tcp_args_aux);
    PTHREAD_ERROR_HELPER(ret, "[ERROR] Failed to create TCP client thread!!!");
  }

  ERROR_HELPER(tcp_client_desc, "[ERROR] Failed to accept client TCP connection!!!");
  pthread_exit(0);
}

void* UDP_receiver_handler(void* args)
{

  int ret;
  udp_args_t* udp_args = (udp_args_t*)args;
  int udp_socket = udp_args->udp_socket;
  char buffer_recv[BUFFER_SIZE];

  struct sockaddr_in client_addr = {0};
  socklen_t addrlen = sizeof(struct sockaddr_in);
  while (1) {
    // printf("[UDP RECEIVER] Waiting packets...\n");

    if ((ret = recvfrom(udp_socket, buffer_recv, BUFFER_SIZE, 0,
                        (struct sockaddr*)&client_addr, &addrlen)) > 0) {
    }  // printf("[UDP RECEIVER] Packet received...\n");
    ERROR_HELPER(ret, "[ERROR] Error receiving UDP packet!!!");

    // Raccoglie il pacchetto ricevuto
    PacketHeader* header = (PacketHeader*)buffer_recv;

    VehicleUpdatePacket* packet =
        (VehicleUpdatePacket*)Packet_deserialize(buffer_recv, header->size);
    User* user = User_find_id(users, packet->id);
    user->user_addr_udp = client_addr;

    // printf("[UDP RECEIVER] Update received from user (%d)...\n", user->id);

    if (!user) {
      printf("[ERROR] Cannot find a user with this ID: %d!!!\n", packet->id);
      pthread_exit(0);
    }

    // Aggiorna la posizione dell'utente
    Vehicle* vehicle_aux = World_getVehicle(&world, user->id);

    vehicle_aux->translational_force_update = packet->translational_force;
    vehicle_aux->rotational_force_update = packet->rotational_force;
    // printf("[UDP RECEIVER] Forces updated (%d)...\n", user->id);

    // Update del mondo
    World_update(&world);
    // printf("[UDP RECEIVER] World updated...\n");
  }

  printf("[UDP RECEIVER] Closing receiver...\n");

  // Packet_free(&packet->header);	// Liberazione memoria utilizzata
  pthread_exit(0);
}

void* UDP_sender_handler(void* args)
{
  printf("[UDP Sender Handler] Handler started...\n");
  char buffer_send[BUFFER_SIZE];

  udp_args_t* udp_args = (udp_args_t*)args;
  int udp_socket = udp_args->udp_socket;

  printf("[UDP SENDER] Ready to send updates...\n");
  while (1) {
    int n_users = users->size;
    if (n_users > 0) {
      PacketHeader header;
      header.type = WorldUpdate;

      WorldUpdatePacket* world_update =
          (WorldUpdatePacket*)malloc(sizeof(WorldUpdatePacket));
      world_update->header = header;
      world_update->updates =
          (ClientUpdate*)malloc(sizeof(ClientUpdate) * n_users);
      world_update->num_vehicles = users->size;

      User* user = users->first;

      for (int i = 0; i < n_users; i++) {
        ClientUpdate* client = &(world_update->updates[i]);
        user->vehicle = World_getVehicle(&world, user->id);
        client->id = user->id;
        client->x = user->vehicle->x;
        client->y = user->vehicle->y;
        client->theta = user->vehicle->theta;
        user = user->next;
      }
      int size = Packet_serialize(buffer_send, &world_update->header);

      user = users->first;

      while (user != NULL) {
        // printf("[UDP SENDER] Sending update to %d...\n", user->id);
        if (user->user_addr_udp.sin_addr.s_addr != 0) {
          int ret = sendto(udp_socket, buffer_send, size, 0,
                           (struct sockaddr*)&user->user_addr_udp,
                           (socklen_t)sizeof(user->user_addr_udp));
          ERROR_HELPER(ret, "[ERROR] Error sending update to user!!!");
        }

        // printf("[UDP SENDER] Update sent to the user (%d)...\n", user->id);

        user = user->next;
      }
    }

    // printf("[UDP SENDER] Wait for next update to send...\n");

    usleep(TIME_TO_SLEEP);
  }

  pthread_exit(0);
}

void* UpdateLoop(void* args)
{
  printf("[Update Loop] Started\n");
  while(1){
    World_update(&world);
    usleep(80000);
  }
} 

// Mark - Main

int main(int argc, char** argv)
{
  int ret;
  int serverTCP, serverUDP;

  if (argc < 3) {
    printf("usage: %s <elevation_image> <texture_image>\n", argv[1]);
    exit(-1);
  }
  char* elevation_filename = argv[1];
  char* texture_filename = argv[2];
  char* vehicle_texture_filename = "./images/arrow-right.ppm";
  printf("loading elevation image from %s ... ", elevation_filename);

  // load the images
  Image* surface_elevation = Image_load(elevation_filename);
  if (surface_elevation) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  printf("loading texture image from %s ... ", texture_filename);
  Image* surface_texture = Image_load(texture_filename);
  if (surface_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  printf("loading vehicle texture (default) from %s ... ",
         vehicle_texture_filename);
  Image* vehicle_texture = Image_load(vehicle_texture_filename);
  if (vehicle_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  // TCP Init
  serverTCP = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(serverTCP, "[TCP] Failed to create TCP socket");
  if (serverTCP >= 0) printf("[TCP] Socket opened (%d)...\n", serverTCP);

  struct sockaddr_in tcp_server_addr = {0};
  int sockaddr_len = sizeof(struct sockaddr_in);
  tcp_server_addr.sin_addr.s_addr = INADDR_ANY;
  tcp_server_addr.sin_family = AF_INET;
  tcp_server_addr.sin_port = htons(TCP_PORT);

  int reuseaddr_opt_tcp = 1;
  ret = setsockopt(serverTCP, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt_tcp,
                   sizeof(reuseaddr_opt_tcp));
  ERROR_HELPER(ret, "[ERROR] Failed setsockopt on TCP server socket!!!");

  ret = bind(serverTCP, (struct sockaddr*)&tcp_server_addr, sockaddr_len);
  ERROR_HELPER(ret, "[ERROR] Failed bind address on TCP server socket!!!");

  ret = listen(serverTCP, 3);
  ERROR_HELPER(ret, "[ERROR] Failed listen on TCP server socket!!!");
  if (ret >= 0) printf("[MAIN] Server listening on port %d...\n", TCP_PORT);

  // UDP Init
  serverUDP = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(serverUDP, "[ERROR] Failed to create UDP socket!!!");

  struct sockaddr_in udp_server_addr = {0};
  udp_server_addr.sin_addr.s_addr = INADDR_ANY;
  udp_server_addr.sin_family = AF_INET;
  udp_server_addr.sin_port = htons(UDP_PORT);

  int reuseaddr_opt_udp = 1;
  ret = setsockopt(serverUDP, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt_udp,
                   sizeof(reuseaddr_opt_udp));
  ERROR_HELPER(ret, "[ERROR] Failed setsockopt on UDP server socket!!!");

  ret = bind(serverUDP, (struct sockaddr*)&udp_server_addr,
             sizeof(udp_server_addr));
  ERROR_HELPER(ret, "[ERROR] Failed bind address on UDP server socket!!!");

  printf("[MAIN] Server UDP started...\n"); 

  users = (UserHead*)malloc(sizeof(UserHead));
  Users_init(users);
  World_init(&world, surface_elevation, surface_texture, 0.5, 0.5, 0.5);
  pthread_t TCP_connection, UDP_sender_thread, UDP_receiver_thread, world_update;

  // Args TCP
  tcp_args_t tcp_args;
  tcp_args.elevation_texture = surface_texture;
  tcp_args.surface_elevation = surface_elevation;
  tcp_args.tcp_socket = serverTCP;

  udp_args_t udp_args;
  udp_args.udp_socket = serverUDP;

  // Threads
  ret = pthread_create(&TCP_connection, NULL, TCP_handler, &tcp_args);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create TCP connection thread");

  ret = pthread_create(&UDP_sender_thread, NULL, UDP_sender_handler, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create UDP sender thread");

  ret = pthread_create(&UDP_receiver_thread, NULL, UDP_receiver_handler, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create UDP receiver thread");

  ret = pthread_create(&world_update, NULL, UpdateLoop, NULL);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create World Update thread");


  // Join dei thread
  ret = pthread_join(world_update, NULL);
  ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join World Updatethread");

  ret = pthread_join(TCP_connection, NULL);
  ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join TCP server connection thread");

  ret = pthread_join(UDP_sender_thread, NULL);
  ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join UDP server sender thread");

  ret = pthread_join(UDP_receiver_thread, NULL);
  ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join UDP server receiver thread");

  // Cleanup
  Image_free(surface_texture);
  Image_free(surface_elevation);
  World_destroy(&world);
  return 0;
}