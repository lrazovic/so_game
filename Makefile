CCOPTS= -Wall -Wstrict-prototypes
LIBS= -lglut -lGLU -lGL -lm -lpthread
CC=gcc
AR=ar


BINS=libso_game.a\
     so_game_server\
     so_game_client
	
OBJS = vec3.o\
       surface.o\
       image.o\
       world.o\
       world_viewer.o\
	   linked_list.o\
	   vehicle.o\
	   so_game_protocol.o\
	   client_list.o\
       client_op.o
       
HEADERS=image.h\
	linked_list.h\
	so_game_protocol.h\
	vehicle.h\
	client_list.h\
	surface.h\
	vec3.h\
	world.h\
	world_viewer.h\
	common.h\
	client_op.h

%.o:	%.c $(HEADERS)
	$(CC) $(CCOPTS) -c -o $@ $<

.phony: clean all

all:	$(BINS)

libso_game.a: $(OBJS)
	$(AR) -rcs $@ $^
	$(RM) $(OBJS)

so_game_client: so_game_client.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)

so_game_server: so_game_server.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)

test_packets_serialization: test_packets_serialization.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^  $(LIBS)

test_client_list: test_client_list.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^  $(LIBS)

test-server:
	./so_game_server images/maze.pgm images/maze.ppm 8888

test-client:
	./so_game_client images/arrow-right.ppm 8888

clean:
	rm -rf *.o *~  $(BINS)

