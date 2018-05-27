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
#include "client_list.h"
#include "vehicle.h"
#include "world.h"
#include "world_viewer.h"
#define HIDE_RANGE 5
// Mark - Structs and Variables
#define SENDER_SLEEP 300000
typedef struct
{
  int client_desc;
  struct sockaddr_in client_addr;
  Image *elevation_texture;
  Image *surface_elevation;
  int tcp_socket;
} tcp_args_t;

typedef struct
{
  int udp_socket;
} udp_args_t;

World world;
ClientListHead *users;
char connectivity = 1;
char has_users = 0;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
// Mark - Network

int UDPHandler(int socket_udp, char *buf_rcv, struct sockaddr_in client_addr)
{
  PacketHeader *ph = (PacketHeader *)buf_rcv;
  switch (ph->type)
  {
  case (VehicleUpdate):
  {
    VehicleUpdatePacket *vup =
        (VehicleUpdatePacket *)Packet_deserialize(buf_rcv, ph->size);
    pthread_mutex_lock(&users_mutex);
    ClientListItem *client = ClientList_find_by_id(users, vup->id);
    if (client == NULL)
    {
      printf(
          "[UDPHandler] Can't find the user with id %d to apply the update "
          "\n",
          vup->id);
      Packet_free(&vup->header);
      pthread_mutex_unlock(&users_mutex);
      ERROR_HELPER(-1, "Receied update from client with invalid id");
      return -1;
    }

    if (!client->is_udp_addr_ready)
    {
      int sockaddr_len = sizeof(struct sockaddr_in);
      char addr_udp[sockaddr_len];
      char addr_tcp[sockaddr_len];
      const char *pt_addr_udp =
          inet_ntop(client_addr.sin_family, &(client_addr.sin_addr), addr_udp,
                    sockaddr_len);
      const char *pt_addr_tcp =
          inet_ntop(client_addr.sin_family, &(client->user_addr_tcp.sin_addr),
                    addr_tcp, sockaddr_len);
      if (pt_addr_udp != NULL && pt_addr_tcp != NULL &&
          strcmp(addr_udp, addr_tcp) != 0)
        goto END;
      client->user_addr_udp = client_addr;
      client->is_udp_addr_ready = 1;
    }

    pthread_mutex_lock(&client->vehicle->mutex);
    Vehicle_setForcesUpdate(client->vehicle, vup->translational_force,
                            vup->rotational_force);
    pthread_mutex_unlock(&client->vehicle->mutex);
  END:
    pthread_mutex_unlock(&users_mutex);
    printf(
        "[UDP_Receiver] Applied VehicleUpdatePacket with "
        "force_translational_update: %f force_rotation_update: %f.. \n",
        vup->translational_force, vup->rotational_force);
    Packet_free(&vup->header);
    return 0;
  }
  default:
    return -1;
  }
}

void* UDPSender(void* args) {
  int socket_udp = *(int*)args;
  while (connectivity) {
    if (!has_users) {
      usleep(SENDER_SLEEP_S);
      continue;
    }
    pthread_mutex_lock(&users_mutex);
    ClientListItem* client = users->first;
    printf("I'm going to create a WorldUpdatePacket \n");
    client = users->first;
    while (client != NULL) {
      char buf_send[BUFFER_SIZE];
      if (client->is_udp_addr_ready != 1) {
        client = client->next;
        continue;
      }
      PacketHeader ph;
      ph.type = WorldUpdate;
      WorldUpdatePacket* wup =
          (WorldUpdatePacket*)malloc(sizeof(WorldUpdatePacket));
      wup->header = ph;
      int n = 0;

      // refresh list x,y,theta before proceding
      ClientListItem* check = users->first;
      while (check != NULL) {
        if (check->is_udp_addr_ready) {
          pthread_mutex_lock(&check->vehicle->mutex);
          Vehicle_getXYTheta(check->vehicle, &check->x, &check->y,
                             &check->theta);
          Vehicle_getForcesUpdate(check->vehicle, &check->translational_force,
                                  &check->rotational_force);
          pthread_mutex_unlock(&check->vehicle->mutex);
        }
        check = check->next;
      }

      // find num of eligible clients to receive the worldUpdatePacket
      ClientListItem* tmp = users->first;
      while (tmp != NULL) {
        if (tmp->is_udp_addr_ready &&
            tmp->id == client->id)
          n++;
        else if (tmp->is_udp_addr_ready &&
                 (abs(tmp->x - client->x) <= HIDE_RANGE &&
                  abs(tmp->y - client->y) <= HIDE_RANGE)) {
          n++;
        }
        tmp = tmp->next;
      }
      if (n == 0) {
        client = client->next;
        free(wup);
        continue;
      }
      wup->num_vehicles = n;
      wup->updates = (ClientUpdate*)malloc(sizeof(ClientUpdate) * n);
      tmp = users->first;
      ClientList_print(users);
      int k = 0;
      // Place data in the WorldUpdatePacket
      while (tmp != NULL) {
        if (!(tmp->is_udp_addr_ready &&
              (abs(tmp->x - client->x) <= HIDE_RANGE &&
               abs(tmp->y - client->y) <= HIDE_RANGE))) {
          tmp = tmp->next;
          continue;
        }
        ClientUpdate* cup = &(wup->updates[k]);
        cup->y = tmp->y;
        cup->x = tmp->x;
        cup->theta = tmp->theta;
        cup->id = tmp->id;
        cup->client_creation_time = tmp->creation_time;
        printf("--- Vehicle with id: %d x: %f y:%f z:%f --- \n", cup->id,
               cup->x, cup->y, cup->theta);
        tmp = tmp->next;
        k++;
      }
      int size = Packet_serialize(buf_send, &wup->header);
      if (size == 0 || size == -1) goto END;
      int ret = sendto(socket_udp, buf_send, size, 0,
                       (struct sockaddr*)&client->user_addr_udp,
                       (socklen_t)sizeof(client->user_addr_udp));
      printf(
          "[UDP_Send] Sent WorldUpdate of %d bytes to client with id %d \n",
          ret, client->id);
      printf("Difference lenght check - wup: %d client found:%d \n",
                  wup->num_vehicles, n);
    END:
      Packet_free(&(wup->header));
      client = client->next;
    }
    fprintf(stdout, "[UDP_Send] WorldUpdatePacket sent to each client \n");
    pthread_mutex_unlock(&users_mutex);
    usleep(SENDER_SLEEP_S);
  }
  pthread_exit(NULL);
}

void *UDPReceiver(void *args)
{
  int socket_udp = *(int *)args;
  while (connectivity)
  {
    if (!has_users)
    {
      usleep(RECEIVER_SLEEP_S);
      continue;
    }
    char buf_recv[BUFFER_SIZE];
    struct sockaddr_in client_addr = {0};
    socklen_t addrlen = sizeof(struct sockaddr_in);
    int bytes_read = recvfrom(socket_udp, buf_recv, BUFFER_SIZE, 0,
                              (struct sockaddr *)&client_addr, &addrlen);
    if (bytes_read == -1)
      goto END;
    if (bytes_read == 0)
      goto END;
    PacketHeader *ph = (PacketHeader *)buf_recv;
    if (ph->size != bytes_read)
    {
      printf("[WARNING] Skipping partial UDP packet \n");
      continue;
    }
    int ret = UDPHandler(socket_udp, buf_recv, client_addr);
    if (ret == -1)
      printf(
          "[UDP_Receiver] UDP Handler couldn't manage to apply the "
          "VehicleUpdate \n");
  END:
    usleep(RECEIVER_SLEEP_S);
  }
  pthread_exit(NULL);
}

int TCPCore(int tcp_socket, char *buffer, Image *surface_elevation,
            Image *elevation_texture, int id, ClientListItem *user)
{
  PacketHeader *header = (PacketHeader *)buffer;
  if (header->type == GetId)
  {
    printf("[TCP] ID requested from (%d)...\n", id);
    IdPacket *id_to_send = (IdPacket *)malloc(sizeof(IdPacket));

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
    while (bytes_sent < pckt_length)
    {
      ret = send(tcp_socket, buffer_send + bytes_sent, pckt_length - bytes_sent,
                 0);
      if (ret == -1 && errno == EINTR)
        continue;
      ERROR_HELPER(ret, "[ERROR] Error assigning ID!!!");
      if (ret == 0)
        break;
      bytes_sent += ret;
    }

    printf("[TCP] ID sent to (%d)...\n", id); // DEBUG OUTPUT

    return 1;
  }
  else if (header->type == GetTexture)
  {
    printf("[TCP] Texture requested from (%d)...\n", id);

    // Converto il pacchetto ricevuto in un ImagePacket per estrarne la texture
    // richiesta
    ImagePacket *texture_request = (ImagePacket *)buffer;
    int id_request = texture_request->id;

    // Preparo header per la risposta
    PacketHeader header_send;
    header_send.type = PostTexture;

    // Preparo il pacchetto per inviare la texture al client
    ImagePacket *texture_to_send = (ImagePacket *)malloc(sizeof(ImagePacket));
    texture_to_send->header = header_send;
    texture_to_send->id = id_request;
    texture_to_send->image = elevation_texture;

    char buffer_send[BUFFER_SIZE];
    int pckt_length = Packet_serialize(
        buffer_send,
        &(texture_to_send->header)); // Ritorna il numero di bytes scritti

    // Invio del messaggio tramite socket
    int bytes_sent = 0;
    int ret;
    while (bytes_sent < pckt_length)
    {
      ret = send(tcp_socket, buffer_send + bytes_sent, pckt_length - bytes_sent,
                 0);
      if (ret == -1 && errno == EINTR)
        continue;
      ERROR_HELPER(ret, "[ERROR] Error requesting texture!!!");
      if (ret == 0)
        break;
      bytes_sent += ret;
    }

    // Packet_free(&(texture_to_send->header));   // Libera la memoria del
    // pacchetto non più utilizzato free(texture_to_send);

    printf("[TCP] Texture sent to (%d)...\n", id); // DEBUG OUTPUT

    return 1;
  }
  else if (header->type == GetElevation)
  {
    printf("[TCP] Elevation requested from (%d)...\n", id);

    // Converto il pacchetto ricevuto in un ImagePacket per estrarne la
    // elevation richiesta
    ImagePacket *elevation_request = (ImagePacket *)buffer;
    int id_request = elevation_request->id;

    // Preparo header per la risposta
    PacketHeader header_send;
    header_send.type = PostElevation;

    // Preparo il pacchetto per inviare la elevation al client
    ImagePacket *elevation_to_send = (ImagePacket *)malloc(sizeof(ImagePacket));
    elevation_to_send->header = header_send;
    elevation_to_send->id = id_request;
    elevation_to_send->image = surface_elevation;

    char buffer_send[BUFFER_SIZE];
    int pckt_length = Packet_serialize(
        buffer_send,
        &(elevation_to_send->header)); // Ritorna il numero di bytes scritti

    // Invio del messaggio tramite socket
    int bytes_sent = 0;
    int ret;
    while (bytes_sent < pckt_length)
    {
      ret = send(tcp_socket, buffer_send + bytes_sent, pckt_length - bytes_sent,
                 0);
      if (ret == -1 && errno == EINTR)
        continue;
      ERROR_HELPER(ret, "[ERROR] Error requesting elevation texture!!!");
      if (ret == 0)
        break;
      bytes_sent += ret;
    }

    // Packet_free(&(elevation_to_send->header));   // Libera la memoria del
    // pacchetto non più utilizzato free(elevation_to_send);

    printf("[TCP] Elevation sent to (%d)...\n", id); // DEBUG OUTPUT

    return 1;
  }
  else if (header->type == PostTexture)
  {
    int ret;

    if (BUFFER_SIZE < header->size)
      return -1;
    //AGGIUNGERE PACKET CLEANUP

    // Deserializzazione del pacchetto
    PacketHeader *received_header = Packet_deserialize(buffer, header->size);
    ImagePacket *received_texture = (ImagePacket *)received_header;

    printf("[TCP] Vehicle sent from (%d)...\n", id);

    // Aggiunta veicolo nuovo al mondo
    Vehicle *new_vehicle = malloc(sizeof(Vehicle));
    user->vehicle = new_vehicle;
    Vehicle_init(new_vehicle, &world, id, received_texture->image);
    World_addVehicle(&world, new_vehicle);

    // Rimanda la texture al client come conferma
    PacketHeader header_aux;
    header_aux.type = PostTexture;

    ImagePacket *texture_for_client = (ImagePacket *)malloc(sizeof(ImagePacket));
    texture_for_client->image = received_texture->image;
    texture_for_client->header = header_aux;

    // Serializza la texture da mandare
    int buffer_size = Packet_serialize(buffer, &texture_for_client->header);

    // Invia la texture
    while ((ret = send(tcp_socket, buffer, buffer_size, 0)) < 0)
    {
      if (errno == EINTR)
        continue;
      ERROR_HELPER(ret, "[ERROR] Cannot write to socket!!!");
    }

    printf("[TCP] Vehicle texture sent back to (%d)...\n", id);

    // Packet_free(&received_texture->header); // Libera la memoria del
    // pacchetto non più utilizzato Packet_free(&texture_for_client->header);
    pthread_mutex_lock(&users_mutex);
    ClientList_insert(users, user);
    ClientList_print(users);
    pthread_mutex_unlock(&users_mutex);
    has_users=1;
    printf("[TCP] User (%d) inserted...\n", user->id);

    return 1;
  }
  else
  {
    printf("[ERROR] Unknown packet received from %d!!!\n", id); // DEBUG OUTPUT
  }

  return -1; // Return in caso di errore
}

void *TCPClientHandler(void *args)
{
  tcp_args_t *tcp_args = (tcp_args_t *)args;
  printf("[TCP Client Handler] Handling client with client descriptor (%d)\n", tcp_args->client_desc);
  int tcp_client_desc = tcp_args->client_desc;
  printf("[TCP Client Handler] Creating user with id: %d\n", tcp_client_desc);

  ClientListItem *user = (ClientListItem *)malloc(sizeof(ClientListItem));
  user->id = tcp_client_desc;
  user->user_addr_tcp = tcp_args->client_addr;
  user->x = 0;
  user->y = 0;
  user->theta = 0;
  user->translational_force = 0;
  user->rotational_force = 0;
  user->vehicle = NULL;
  user->v_texture = NULL;

  printf("[New user] Adding client with id %d \n", tcp_client_desc);
  int ph_len = sizeof(PacketHeader);
  int isActive = 1;
  while (connectivity && isActive)
  {
    int msg_len = 0;
    char buf_rcv[BUFFER_SIZE];
    while (msg_len < ph_len)
    {
      int ret = recv(tcp_client_desc, buf_rcv + msg_len, ph_len - msg_len, 0);
      if (ret == -1 && errno == EINTR)
        continue;
      else if (ret <= 0)
        goto EXIT;
      msg_len += ret;
    }
    PacketHeader *header = (PacketHeader *)buf_rcv;
    int size_remaining = header->size - ph_len;
    msg_len = 0;
    while (msg_len < size_remaining)
    {
      int ret = recv(tcp_client_desc, buf_rcv + msg_len + ph_len,
                     size_remaining - msg_len, 0);
      if (ret == -1 && errno == EINTR)
        continue;
      else if (ret <= 0)
        goto EXIT;
      msg_len += ret;
    }
    int ret = TCPCore(tcp_client_desc, buf_rcv, tcp_args->surface_elevation,
                      tcp_args->elevation_texture, tcp_args->client_desc, user);
    if (ret==-1) break;
  }

EXIT:
  printf("Freeing resources...");
  pthread_mutex_lock(&users_mutex);
  ClientListItem *el = ClientList_find_by_id(users, tcp_client_desc);
  if (el == NULL)
    goto END;
  ClientListItem *del = ClientList_detach(users, el);
  if (del == NULL)
    goto END;
  World_detachVehicle(&world, del->vehicle);
  Vehicle_destroy(del->vehicle);
  free(del->vehicle);
  Image *user_texture = del->v_texture;
  if (user_texture != NULL)
    Image_free(user_texture);
  free(del);
  ClientList_print(users);
END:
  if (users->size == 0)
    has_users = 0;
  pthread_mutex_unlock(&users_mutex);
  close(tcp_client_desc);
  pthread_exit(NULL);
}

void *TCP_handler(void *args)
{
  printf("[TCP Handler] Started...\n");
  tcp_args_t *tcp_args = (tcp_args_t *)args;
  int ret;
  int tcp_client_desc;
  int tcp_socket = tcp_args->tcp_socket;

  printf("[TCP Handler] Accepting connection from clients...\n");

  int sockaddr_len = sizeof(struct sockaddr_in);
  struct sockaddr_in client_addr;

  while ((tcp_client_desc = accept(tcp_socket, (struct sockaddr *)&client_addr, (socklen_t *)&sockaddr_len)) > 0)
  {
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

void *UpdateLoop(void *args)
{
  printf("[Update Loop] Started\n");
  while (connectivity)
  {
    World_update(&world);
    usleep(100000);
  }
  pthread_exit(NULL);
}

// Mark - Main

int main(int argc, char **argv)
{
  int ret;
  int serverTCP, serverUDP;

  if (argc < 3)
  {
    printf("usage: %s <elevation_image> <texture_image>\n", argv[1]);
    exit(-1);
  }
  char *elevation_filename = argv[1];
  char *texture_filename = argv[2];
  char *vehicle_texture_filename = "./images/arrow-right.ppm";
  printf("loading elevation image from %s ... ", elevation_filename);

  // load the images
  Image *surface_elevation = Image_load(elevation_filename);
  if (surface_elevation)
  {
    printf("Done! \n");
  }
  else
  {
    printf("Fail! \n");
  }

  printf("loading texture image from %s ... ", texture_filename);
  Image *surface_texture = Image_load(texture_filename);
  if (surface_texture)
  {
    printf("Done! \n");
  }
  else
  {
    printf("Fail! \n");
  }

  printf("loading vehicle texture (default) from %s ... ",
         vehicle_texture_filename);
  Image *vehicle_texture = Image_load(vehicle_texture_filename);
  if (vehicle_texture)
  {
    printf("Done! \n");
  }
  else
  {
    printf("Fail! \n");
  }

  // TCP Init
  serverTCP = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(serverTCP, "[TCP] Failed to create TCP socket");
  if (serverTCP >= 0)
    printf("[TCP] Socket opened (%d)...\n", serverTCP);

  struct sockaddr_in tcp_server_addr = {0};
  int sockaddr_len = sizeof(struct sockaddr_in);
  tcp_server_addr.sin_addr.s_addr = INADDR_ANY;
  tcp_server_addr.sin_family = AF_INET;
  tcp_server_addr.sin_port = htons(TCP_PORT);

  int reuseaddr_opt_tcp = 1;
  ret = setsockopt(serverTCP, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt_tcp,
                   sizeof(reuseaddr_opt_tcp));
  ERROR_HELPER(ret, "[ERROR] Failed setsockopt on TCP server socket!!!");

  ret = bind(serverTCP, (struct sockaddr *)&tcp_server_addr, sockaddr_len);
  ERROR_HELPER(ret, "[ERROR] Failed bind address on TCP server socket!!!");

  ret = listen(serverTCP, 3);
  ERROR_HELPER(ret, "[ERROR] Failed listen on TCP server socket!!!");
  if (ret >= 0)
    printf("[MAIN] Server listening on port %d...\n", TCP_PORT);

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

  ret = bind(serverUDP, (struct sockaddr *)&udp_server_addr,
             sizeof(udp_server_addr));
  ERROR_HELPER(ret, "[ERROR] Failed bind address on UDP server socket!!!");

  printf("[MAIN] Server UDP started...\n");

  users = (ClientListHead *)malloc(sizeof(ClientListHead));
  ClientList_init(users);
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

  ret = pthread_create(&UDP_sender_thread, NULL, UDPSender, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create UDP sender thread");

  ret = pthread_create(&UDP_receiver_thread, NULL, UDPReceiver, &udp_args);
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