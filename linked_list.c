#include "linked_list.h"
#include <assert.h>

void List_init(ListHead* head) {
  head->first = 0;
  head->last = 0;
  head->size = 0;
  sem_init(&(head->sem), 0, 1);
}

ListItem* List_find(ListHead* head, ListItem* item) {
  // linear scanning of list
  sem_wait(&(head->sem));
  ListItem* aux = head->first;
  while (aux) {
    if (aux == item) {
      sem_post(&(head->sem));
      return item;
    }
    aux = aux->next;
  }
  sem_post(&(head->sem));
  return 0;
}

ListItem* List_insert(ListHead* head, ListItem* prev, ListItem* item) {
  sem_wait(&(head->sem));
  if (item->next || item->prev) {
    sem_post(&(head->sem));
    return 0;
  }

  ListItem* next = prev ? prev->next : head->first;
  if (prev) {
    item->prev = prev;
    prev->next = item;
  }
  if (next) {
    item->next = next;
    next->prev = item;
  }
  if (!prev) head->first = item;
  if (!next) head->last = item;
  ++head->size;
  sem_post(&(head->sem));
  return item;
}

ListItem* List_detach(ListHead* head, ListItem* item) {
  sem_wait(&(head->sem));
  ListItem* prev = item->prev;
  ListItem* next = item->next;
  if (prev) {
    prev->next = next;
  }
  if (next) {
    next->prev = prev;
  }
  if (item == head->first) head->first = next;
  if (item == head->last) head->last = prev;
  head->size--;
  item->next = item->prev = 0;
  sem_post(&(head->sem));
  return item;
}
