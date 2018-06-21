#include <arpa/inet.h>  // htons() and inet_addr()
#include <math.h>
#include <netinet/in.h>  // struct sockaddr_in
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "common.h"
#include "image.h"
#include "so_game_protocol.h"
#include "surface.h"
#include "vehicle.h"
#include "world.h"
#include "world_viewer.h"

// Richiedere ID
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
  printf("[Get Id] Received %dbytes \n", msg_len + ph_len);
  int id = deserialized_packet->id;
  Packet_free(&(deserialized_packet->header));
  return id;
}

// Richiedere Elevation Map
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
  Image* ris = deserialized_packet->image;
  free(deserialized_packet);
  return ris;
}

// Richiedere TextureMap
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
  Image* ris = deserialized_packet->image;
  free(deserialized_packet);
  return ris;
}

// Inviare Texture del Veicolo
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

// Richiedere Texture Veicolo
Image* getVehicleTexture(int socket, int id) {
  char buf_send[BUFFERSIZE];
  char buf_rcv[BUFFERSIZE];
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
  char flag = 0;
  if (header->type == PostDisconnect) flag = 1;
  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(msg_len, "Cannot read from socket");
    msg_len += ret;
  }

  if (flag) {
    IdPacket* packet = (IdPacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
    Packet_free(&packet->header);
    return NULL;
  }
  ImagePacket* deserialized_packet =
      (ImagePacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[Get Vehicle Texture] Received %d bytes \n", msg_len + ph_len);
  Image* im = deserialized_packet->image;
  free(deserialized_packet);
  return im;
}

// Gestire la Disconnessione
int sendGoodbye(int socket, int id) {
  char buf_send[BUFFERSIZE];
  IdPacket* idpckt = (IdPacket*)malloc(sizeof(IdPacket));
  PacketHeader ph;
  ph.type = PostDisconnect;
  idpckt->id = id;
  idpckt->header = ph;
  int size = Packet_serialize(buf_send, &(idpckt->header));
  printf("[Goodbye] Sending goodbye  \n");
  int msg_len = 0;
  while (msg_len < size) {
    int ret = send(socket, buf_send + msg_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Can't send goodbye");
    if (ret == 0) break;
    msg_len += ret;
  }
  printf("[Goodbye] Goodbye was successfully sent %d \n", msg_len);
  return 0;
}
