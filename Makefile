CCOPTS= -Wall -g -std=gnu99 -Wstrict-prototypes -framework OpenGL -framework GLUT
LIBS= -lm -lpthread 
#CC=gcc
CC=gcc-7
AR=ar

BINS=libso_game.a\
     so_game\
     test_packets_serialization 

OBJS = vec3.o\
       linked_list.o\
       surface.o\
       image.o\
       vehicle.o\
       world.o\
       world_viewer.o\
       so_game_protocol.o\

HEADERS=helpers.h\
	image.h\
	linked_list.h\
	so_game_protocol.h\
	surface.h\
	vec3.h\
	vehicle.h\
	world.h\
	world_viewer.h\

%.o:	%.c $(HEADERS)
	$(CC) $(CCOPTS) -c -o $@  $<

.phony: clean all


all:	$(BINS) 

libso_game.a: $(OBJS) 
	$(AR) -rcs $@ $^
	$(RM) $(OBJS)

so_game: so_game.c libso_game.a 
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)

test_packets_serialization: test_packets_serialization.c libso_game.a  
	$(CC) $(CCOPTS) -Ofast -o $@ $^  $(LIBS)

clean:
	rm -rf *.o *~  $(BINS)

test:
	./so_game images/arrow-right.ppm images/maze.ppm
test-packet:
	./test_packets_serialization images/arrow-right.ppm images/maze.ppm

