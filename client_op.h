#pragma once
#include "so_game_protocol.h"

int getID(int socket);
Image* getElevationMap(int socket);
Image* getTextureMap(int socket);
int sendVehicleTexture(int socket, Image* texture, int id);
int sendGoodbye(int socket,int id);
Image* getVehicleTexture(int socket, int id);
