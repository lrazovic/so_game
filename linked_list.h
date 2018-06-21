#pragma once
#include <netinet/in.h>
#include <semaphore.h>
#include <time.h>
#include "image.h"
typedef struct ListItem {
  struct ListItem* prev;
  struct ListItem* next;
} ListItem;

typedef struct ListHead {
  ListItem* first;
  ListItem* last;
  int size;
  sem_t sem;
} ListHead;

void List_init(ListHead* head);
ListItem* List_find_by_id(ListHead* head, int id);
ListItem* List_find(ListHead* head, ListItem* item);
ListItem* List_insert(ListHead* head, ListItem* previous, ListItem* item);
ListItem* List_detach(ListHead* head, ListItem* item);
void List_destroy(ListHead* head);
