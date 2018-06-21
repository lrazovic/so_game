#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "client_list.h"
#include "client_op.h"
#include "common.h"
#include "image.h"
#include "so_game_protocol.h"
#include "surface.h"
#include "vehicle.h"
#include "world.h"
#include "world_viewer.h"

/// Mark - Variables

// World
World server_world;
struct timeval world_update_time;

// Network
uint16_t port_number_no;
int server_tcp = -1;
int server_udp;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;

// Flags
int connectivity = 1;
int exchange_update = 1;
int has_users = 0;
ClientListHead* users;

/// Mark - Struct

typedef struct {
  int client_desc;
  Image* elevation_texture;
  Image* surface_texture;
  struct sockaddr_in client_addr_tcp;
} tcpArgs;

/// Mark - Functions

// Invia un Packet PostDisconnect per gestire la disconnessione
void sendDisconnect(int socket_udp, struct sockaddr_in client_addr) {
  char buf_send[BUFFERSIZE];
  PacketHeader ph;
  ph.type = PostDisconnect;
  IdPacket* ip = (IdPacket*)malloc(sizeof(IdPacket));
  ip->id = -1;
  ip->header = ph;
  int size = Packet_serialize(buf_send, &(ip->header));
  int ret =
      sendto(socket_udp, buf_send, size, 0, (struct sockaddr*)&client_addr,
             (socklen_t)sizeof(client_addr));
  Packet_free(&(ip->header));
  printf(
      "[UDP_Receiver] Sent PostDisconnect packet of %d bytes to unrecognized "
      "user \n",
      ret);
}

int UDPHandler(int socket_udp, char* buf_rcv, struct sockaddr_in client_addr) {
  PacketHeader* ph = (PacketHeader*)buf_rcv;
  if(ph->type == VehicleUpdate) {
      VehicleUpdatePacket* vup =
          (VehicleUpdatePacket*)Packet_deserialize(buf_rcv, ph->size);
      pthread_mutex_lock(&users_mutex);
      ClientListItem* client = ClientList_find_by_id(users, vup->id);
      if (!client->is_udp_addr_ready) {
        int sockaddr_len = sizeof(struct sockaddr_in);
        char addr_udp[sockaddr_len];
        char addr_tcp[sockaddr_len];
        inet_ntop(client_addr.sin_family, &(client_addr.sin_addr), addr_udp,
                      sockaddr_len);
        inet_ntop(client_addr.sin_family, &(client->user_addr_tcp.sin_addr),
                      addr_tcp, sockaddr_len);
        client->user_addr_udp = client_addr;
        client->is_udp_addr_ready = 1;
      }
      pthread_mutex_lock(&client->vehicle->mutex);
      Vehicle_addForcesUpdate(client->vehicle, vup->translational_force,
                              vup->rotational_force);
      World_manualUpdate(&server_world, client->vehicle, vup->time);
      pthread_mutex_unlock(&client->vehicle->mutex);
      if (client->prev_x != -1 && client->prev_y != -1) {
        client->x_shift += abs(client->x - client->prev_x);
        client->y_shift += abs(client->y - client->prev_y);
      }
      client->prev_x = client->x;
      client->prev_y = client->y;
      client->last_update_time = vup->time;
      fprintf(stdout,
              "[UDP_Receiver] Applied VehicleUpdatePacket with "
              "force_translational_update: %f force_rotation_update: %f.. \n",
              vup->translational_force, vup->rotational_force);
      pthread_mutex_unlock(&users_mutex);
      Packet_free(&vup->header);
      return 0;
    } else return -1; // Pacchetto mal formato
}

int TCPHandler(int socket_desc, char* buf_rcv, Image* texture_map,
               Image* elevation_map, int id, int* isActive) {
  PacketHeader* header = (PacketHeader*)buf_rcv;
  switch (header->type) {
    case (GetId): {
      char buf_send[BUFFERSIZE];
      IdPacket* response = (IdPacket*)malloc(sizeof(IdPacket));
      PacketHeader ph;
      ph.type = GetId;
      response->header = ph;
      response->id = id;
      int msg_len = Packet_serialize(buf_send, &(response->header));
      printf("[Send ID] bytes written in the buffer: %d\n", msg_len);
      int ret = 0;
      int bytes_sent = 0;
      while (bytes_sent < msg_len) {
        ret = send(socket_desc, buf_send + bytes_sent, msg_len - bytes_sent, 0);
        if (ret == -1 && errno == EINTR) continue;
        ERROR_HELPER(ret, "Can't assign ID");
        if (ret == 0) break;
        bytes_sent += ret;
      }
      Packet_free(&(response->header));
      printf("[Send ID] Sent %d bytes \n", bytes_sent);
      return 0;
    }
    case (GetTexture): {
      char buf_send[BUFFERSIZE];
      ImagePacket* image_request = (ImagePacket*)buf_rcv;
      if (image_request->id >= 0) {
        if (image_request->id == 0)
          printf(
              "[WARNING] Received GetTexture with id 0 which is highly "
              "unlikeable \n");
        char buf_send[BUFFERSIZE];
        ImagePacket* image_packet = (ImagePacket*)malloc(sizeof(ImagePacket));
        PacketHeader im_head;
        im_head.type = PostTexture;
        pthread_mutex_lock(&users_mutex);
        ClientListItem* el = ClientList_find_by_id(users, image_request->id);

        if (el == NULL) {
          pthread_mutex_unlock(&users_mutex);
          PacketHeader pheader;
          pheader.type = PostDisconnect;
          IdPacket* id_pckt = (IdPacket*)malloc(sizeof(IdPacket));
          id_pckt->header = pheader;
          int msg_len = Packet_serialize(buf_send, &id_pckt->header);
          id_pckt->id = -1;
          int bytes_sent = 0;
          int ret = 0;
          while (bytes_sent < msg_len) {
            ret = send(socket_desc, buf_send + bytes_sent, msg_len - bytes_sent,
                       0);
            if (ret == -1 && errno == EINTR) continue;
            ERROR_HELPER(ret, "Can't send map texture over TCP");
            bytes_sent += ret;
          }
          free(id_pckt);
          free(image_packet);
          return -1;
        }

        image_packet->id = image_request->id;
        image_packet->image = el->v_texture;
        pthread_mutex_unlock(&users_mutex);
        image_packet->header = im_head;
        int msg_len = Packet_serialize(buf_send, &image_packet->header);
        printf("[Send Vehicle Texture] bytes written in the buffer: %d\n",
               msg_len);
        int bytes_sent = 0;
        int ret = 0;
        while (bytes_sent < msg_len) {
          ret =
              send(socket_desc, buf_send + bytes_sent, msg_len - bytes_sent, 0);
          if (ret == -1 && errno == EINTR) continue;
          ERROR_HELPER(ret, "Can't send map texture over TCP");
          bytes_sent += ret;
        }

        free(image_packet);
        printf("[Send Vehicle Texture] Sent %d bytes \n", bytes_sent);
        return 0;
      }
      ImagePacket* image_packet = (ImagePacket*)malloc(sizeof(ImagePacket));
      PacketHeader im_head;
      im_head.type = PostTexture;
      image_packet->image = texture_map;
      image_packet->header = im_head;
      int msg_len = Packet_serialize(buf_send, &image_packet->header);
      printf("[Send Map Texture] bytes written in the buffer: %d\n", msg_len);
      int bytes_sent = 0;
      int ret = 0;
      while (bytes_sent < msg_len) {
        ret = send(socket_desc, buf_send + bytes_sent, msg_len - bytes_sent, 0);
        if (ret == -1 && errno == EINTR) continue;
        ERROR_HELPER(ret, "Can't send map texture over TCP");
        if (ret == 0) break;
        bytes_sent += ret;
      }
      free(image_packet);
      printf("[Send Map Texture] Sent %d bytes \n", bytes_sent);
      return 0;
    }
    case (GetElevation): {
      char buf_send[BUFFERSIZE];
      ImagePacket* image_packet = (ImagePacket*)malloc(sizeof(ImagePacket));
      PacketHeader im_head;
      im_head.type = PostElevation;
      image_packet->image = elevation_map;
      image_packet->header = im_head;
      int msg_len = Packet_serialize(buf_send, &image_packet->header);
      printf("[Send Map Elevation] bytes written in the buffer: %d\n", msg_len);
      int bytes_sent = 0;
      int ret = 0;
      while (bytes_sent < msg_len) {
        ret = send(socket_desc, buf_send + bytes_sent, msg_len - bytes_sent, 0);
        if (ret == -1 && errno == EINTR) continue;
        ERROR_HELPER(ret, "Can't send map elevation over TCP");
        if (ret == 0) break;
        bytes_sent += ret;
      }
      free(image_packet);
      printf("[Send Map Elevation] Sent %d bytes \n", bytes_sent);
      return 0;
    }
    case (PostTexture): {
      ImagePacket* deserialized_packet =
          (ImagePacket*)Packet_deserialize(buf_rcv, header->size);
      Image* user_texture = deserialized_packet->image;

      pthread_mutex_lock(&users_mutex);
      ClientListItem* user =
          ClientList_find_by_id(users, deserialized_packet->id);
      ClientList_print(users);
      fflush(stdout);

      if (user == NULL) {
        printf("[Set Texture] User not found \n");
        pthread_mutex_unlock(&users_mutex);
        Packet_free(&(deserialized_packet->header));
        return -1;
      }
      if (user->inside_world) {
        pthread_mutex_unlock(&users_mutex);
        Packet_free(&(deserialized_packet->header));
        return 0;
      }
      user->v_texture = user_texture;
      user->inside_world = 1;
      Vehicle* vehicle = (Vehicle*)malloc(sizeof(Vehicle));
      Vehicle_init(vehicle, &server_world, id, user->v_texture);
      user->vehicle = vehicle;
      World_addVehicle(&server_world, vehicle);
      pthread_mutex_unlock(&users_mutex);
      printf("[Set Texture] Vehicle texture applied to user with id %d \n", id);
      free(deserialized_packet);
      return 0;
    }
    case (PostDisconnect): {
      printf("[Notify Disconnect] User disconnect...");
      *isActive = 0;
      return 0;
    }
    default: {
      *isActive = 0;
      printf("[TCP Handler] Unknown packet. Cleaning resources...\n");
      return -1;
    }
  }
}

// Gestisce la connessione TCP
void* TCPMaster(void* args) {
  tcpArgs* tcp_args = (tcpArgs*)args;
  int sock_fd = tcp_args->client_desc;
  pthread_mutex_lock(&users_mutex);
  ClientListItem* user = malloc(sizeof(ClientListItem));
  user->v_texture = NULL;
  gettimeofday(&user->creation_time, NULL);
  user->id = sock_fd;
  user->user_addr_tcp = tcp_args->client_addr_tcp;
  user->is_udp_addr_ready = 0;
  user->inside_world = 0;
  user->v_texture = NULL;
  user->vehicle = NULL;
  user->prev_x = -1;
  user->prev_y = -1;
  user->x_shift = -1;
  user->y_shift = -1;
  user->last_update_time.tv_sec = -1;
  printf("[New user] Adding client with id %d \n", sock_fd);
  ClientList_insert(users, user);
  ClientList_print(users);
  has_users = 1;
  pthread_mutex_unlock(&users_mutex);
  int ph_len = sizeof(PacketHeader);
  int isActive = 1;
  while (connectivity && isActive) {
    int msg_len = 0;
    char buf_rcv[BUFFERSIZE];
    while (msg_len < ph_len) {
      int ret = recv(sock_fd, buf_rcv + msg_len, ph_len - msg_len, 0);
      if (ret == -1 && errno == EINTR)
        continue;
      else if (ret <= 0)
        goto EXIT;
      msg_len += ret;
    }

    PacketHeader* header = (PacketHeader*)buf_rcv;
    int size_remaining = header->size - ph_len;
    msg_len = 0;
    while (msg_len < size_remaining) {
      int ret = recv(sock_fd, buf_rcv + msg_len + ph_len,
                     size_remaining - msg_len, 0);
      if (ret == -1 && errno == EINTR)
        continue;
      else if (ret <= 0)
        goto EXIT;
      msg_len += ret;
    }
    int ret = TCPHandler(sock_fd, buf_rcv, tcp_args->surface_texture,
                         tcp_args->elevation_texture, tcp_args->client_desc,
                         &isActive);
    if (ret == -1) ClientList_print(users);
  }
EXIT:
  printf("Freeing resources...");
  pthread_mutex_lock(&users_mutex);
  ClientListItem* el = ClientList_find_by_id(users, sock_fd);
  if (el == NULL) goto END;
  ClientListItem* del = ClientList_detach(users, el);
  if (del == NULL) goto END;
  if (!del->inside_world) goto END;
  World_detachVehicle(&server_world, del->vehicle);
  Vehicle_destroy(del->vehicle);  // Be careful here
  free(del->vehicle);
  Image* user_texture = del->v_texture;
  if (user_texture != NULL) Image_free(user_texture);
  if (users->size == 0) has_users = 0;
  free(del);
END:
  ClientList_print(users);
  pthread_mutex_unlock(&users_mutex);
  close(sock_fd);
  pthread_exit(NULL);
}

// Riceve ed applica i pacchetti VehicleUpdatePacket
void* UDPReceiver(void* args) {
  int socket_udp = *(int*)args;
  while (connectivity && exchange_update) {
    if (!has_users) {
      usleep(RECEIVER_SLEEP_S);
      continue;
    }
    char buf_recv[BUFFERSIZE];
    struct sockaddr_in client_addr = {0};
    socklen_t addrlen = sizeof(struct sockaddr_in);
    int bytes_read = recvfrom(socket_udp, buf_recv, BUFFERSIZE, 0,
                              (struct sockaddr*)&client_addr, &addrlen);
    if (bytes_read == -1) goto END;
    if (bytes_read == 0) goto END;
    PacketHeader* ph = (PacketHeader*)buf_recv;
    if (ph->size != bytes_read) {
      printf("[WARNING] Skipping partial UDP packet \n");
      goto END;
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

// Invia un WorldUpdatePacket ad ogni client
void* UDPSender(void* args) {
  int socket_udp = *(int*)args;
  while (connectivity && exchange_update) {
    if (!has_users) {
      usleep(SENDER_SLEEP_S);
      continue;
    }
    pthread_mutex_lock(&users_mutex);
    ClientListItem* client = users->first;
    printf("I'm going to create a WorldUpdatePacket \n");
    client = users->first;
    struct timeval time;
    gettimeofday(&time, NULL);
    World_update(&server_world);
    while (client != NULL) {
      char buf_send[BUFFERSIZE];
      if (client->is_udp_addr_ready != 1 || !client->inside_world) {
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
        if (check->inside_world && check->is_udp_addr_ready) {
          pthread_mutex_lock(&check->vehicle->mutex);
          Vehicle_getXYTheta(check->vehicle, &check->x, &check->y,
                             &check->theta);
          Vehicle_getForcesUpdate(check->vehicle, &check->translational_force,
                                  &check->rotational_force);
          Vehicle_getTime(check->vehicle, &check->world_update_time);
          pthread_mutex_unlock(&check->vehicle->mutex);
        }
        check = check->next;
      }
      
      // find num of eligible clients to receive the worldUpdatePacket
      ClientListItem* tmp = users->first;
      while (tmp != NULL) {
        if (tmp->is_udp_addr_ready && tmp->inside_world &&
            tmp->id == client->id)
          n++;
        else if (tmp->is_udp_addr_ready && tmp->inside_world &&
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
      wup->time = time;
      tmp = users->first;
      ClientList_print(users);
      int k = 0;
      // Place data in the WorldUpdatePacket
      while (tmp != NULL) {
        if (!(tmp->is_udp_addr_ready && tmp->inside_world &&
              (abs(tmp->x - client->x) <= HIDE_RANGE &&
               abs(tmp->y - client->y) <= HIDE_RANGE))) {
          tmp = tmp->next;
          continue;
        }
        ClientUpdate* cup = &(wup->updates[k]);
        cup->y = tmp->y;
        cup->x = tmp->x;
        cup->theta = tmp->theta;
        cup->rotational_force = tmp->rotational_force;
        cup->translational_force = tmp->translational_force;
        cup->id = tmp->id;
        if (timercmp(&tmp->last_update_time, &tmp->world_update_time, >))
          cup->client_update_time = tmp->last_update_time;
        else
          cup->client_update_time = tmp->world_update_time;
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
      printf("[UDP_Send] Sent WorldUpdate of %d bytes to client with id %d \n",
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

// Autenticazione client
void* TCPAuth(void* args) {
  tcpArgs* tcp_args = (tcpArgs*)args;
  int sockaddr_len = sizeof(struct sockaddr_in);
  while (connectivity) {
    struct sockaddr_in client_addr = {0};
    int client_desc = accept(server_tcp, (struct sockaddr*)&client_addr, (socklen_t*)&sockaddr_len);
    if (client_desc == -1 && errno == EINTR) {
      printf("Errore");
      continue;
    } else if (client_desc == -1)
      break;
    tcpArgs new_tcp_args;
    pthread_t threadTCP;
    new_tcp_args.client_desc = client_desc;
    new_tcp_args.elevation_texture = tcp_args->elevation_texture;
    new_tcp_args.surface_texture = tcp_args->surface_texture;
    new_tcp_args.client_addr_tcp = client_addr;

    // Crea un Thread per ogni client connesso
    int ret = pthread_create(&threadTCP, NULL, TCPMaster, &new_tcp_args);
    PTHREAD_ERROR_HELPER(ret, "[MAIN] pthread_create on thread tcp failed");
    ret = pthread_detach(threadTCP);
  }
  pthread_exit(NULL);
}

// Aggiornamento del mondo
void* worldLoop(void* args) {
  printf("[WorldLoop] World Update loop initialized \n");
  while (connectivity) {
    World_update(&server_world);
    usleep(WORLD_LOOP_SLEEP);
  }
  pthread_exit(NULL);
}

/// Mark - Main

int main(int argc, char** argv) {
  int ret = 0;
  if (argc < 4) {
    printf("usage: %s <elevation_image> <texture_image> <port_number>\n",
           argv[1]);
    exit(-1);
  }
  char* elevation_filename = argv[1];
  char* texture_filename = argv[2];
  long tmp = strtol(argv[3], NULL, 0);
  // load the images
  fprintf(stdout, "[Main] loading elevation image from %s ... ",
          elevation_filename);
  Image* surface_elevation = Image_load(elevation_filename);
  if (surface_elevation) {
    fprintf(stdout, "Done! \n");
  } else {
    fprintf(stdout, "Fail! \n");
  }
  fprintf(stdout, "[Main] loading texture image from %s ... ",
          texture_filename);
  Image* surface_texture = Image_load(texture_filename);
  if (surface_texture) {
    fprintf(stdout, "Done! \n");
  } else {
    fprintf(stdout, "Fail! \n");
  }

  port_number_no = htons((uint16_t)tmp);

  // setup tcp socket
  printf("[Main] Starting TCP socket \n");

  server_tcp = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(server_tcp, "Can't create server_tcp socket");

  struct sockaddr_in server_addr = {0};
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = port_number_no;

  int reuseaddr_opt = 1;  // recover server if a crash occurs
  ret = setsockopt(server_tcp, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt,
                   sizeof(reuseaddr_opt));
  ERROR_HELPER(ret, "Failed setsockopt() on server_tcp socket");

  int sockaddr_len = sizeof(struct sockaddr_in);
  ret = bind(server_tcp, (struct sockaddr*)&server_addr,
             sockaddr_len);  // binding dell'indirizzo
  ERROR_HELPER(ret, "Failed bind() on server_tcp");

  ret = listen(server_tcp, 16);  // flag socket as passive
  ERROR_HELPER(ret, "Failed listen() on server_desc");

  printf("[Main] TCP socket successfully created \n");

  // init List structure
  users = malloc(sizeof(ClientListHead));
  ClientList_init(users);
  fprintf(stdout, "[Main] Initialized users list \n");

  printf("[Main] Custom signal handlers are now enabled \n");

  // setup UDP socket

  uint16_t port_number_no_udp = htons((uint16_t)UDPPORT);
  server_udp = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(server_udp, "Can't create server_udp socket");

  struct sockaddr_in udp_server = {0};
  udp_server.sin_addr.s_addr = INADDR_ANY;
  udp_server.sin_family = AF_INET;
  udp_server.sin_port = port_number_no_udp;

  int reuseaddr_opt_udp = 1;  // recover server if a crash occurs
  ret = setsockopt(server_udp, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt_udp,
                   sizeof(reuseaddr_opt_udp));
  ERROR_HELPER(ret, "Failed setsockopt() on server_udp socket");

  ret = bind(server_udp, (struct sockaddr*)&udp_server,
             sizeof(udp_server));  // binding dell'indirizzo
  ERROR_HELPER(ret, "Failed bind() on server_udp socket");

  printf("[Main] UDP socket created \n");
  tcpArgs tcp_args;
  tcp_args.surface_texture = surface_texture;
  tcp_args.elevation_texture = surface_elevation;
  World_init(&server_world, surface_elevation, surface_texture);

  pthread_t UDP_receiver, UDP_sender, tcp_thread, world_thread;
  ret = pthread_create(&UDP_receiver, NULL, UDPReceiver, &server_udp);
  PTHREAD_ERROR_HELPER(ret, "pthread_create on thread tcp failed");
  ret = pthread_create(&UDP_sender, NULL, UDPSender, &server_udp);
  PTHREAD_ERROR_HELPER(ret, "pthread_create on thread tcp failed");
  ret = pthread_create(&tcp_thread, NULL, TCPAuth, &tcp_args);
  PTHREAD_ERROR_HELPER(ret,
                       "pthread_create on garbace collector thread failed");
  ret = pthread_create(&world_thread, NULL, worldLoop, NULL);
  PTHREAD_ERROR_HELPER(ret, "pthread_create on world_loop thread failed");
  fprintf(stdout,
          "[Main] World created. Now waiting for clients to connect... \n");
  fflush(stdout);

  // Wait for threads to finish
  ret = pthread_join(world_thread, NULL);
  ERROR_HELPER(ret, "Join on world_loop thread failed");
  printf("[Main] World_loop ended... \n");
  ret = pthread_join(UDP_receiver, NULL);
  ERROR_HELPER(ret, "Join on UDP_receiver thread failed");
  printf("[Main] UDP_receiver ended... \n");
  ret = pthread_join(tcp_thread, NULL);
  ERROR_HELPER(ret, "Join on tcp_auth thread failed");
  printf("[Main] TCP_receiver/sender ended... \n");
  ret = pthread_join(UDP_sender, NULL);
  ERROR_HELPER(ret, "Join on UDP_sender thread failed");
  printf("[Main] UDP_sender ended... \n");
  printf("[Main] Freeing resources... \n");
  // Delete list and other structures
  ClientList_destroy(users);
  pthread_mutex_destroy(&users_mutex);
  World_destroy(&server_world);
  // Close descriptors
  ret = close(server_tcp);
  ERROR_HELPER(ret, "Failed close() on server_tcp socket");
  ret = close(server_udp);
  ERROR_HELPER(ret, "Failed close() on server_udp socket");
  World_destroy(&server_world);
  Image_free(surface_elevation);
  Image_free(surface_texture);
  exit(EXIT_SUCCESS);
}
