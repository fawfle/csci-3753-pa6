#include "array.h"
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#ifndef ARRAY_H
#define ARRAY_H

int array_init(array *s) {
  s->front = 0;
  s->back = 0;
  s->length = 0;
  pthread_mutex_init(&s->access_lock, NULL); // initialize mutexes
  sem_init(&s->items_available, 0, 0); // initialize available semaphore to 0 since there are none.
  sem_init(&s->space_available, 0, ARRAY_SIZE); // initialize space available semaphore to the array size.
  return 0;
}

int array_put(array *s, char *hostname) {
  sem_wait(&s->space_available);

  pthread_mutex_lock(&s->access_lock);

  s->queue[s->back] = hostname;
  s->back = (s->back + 1) % ARRAY_SIZE;
  s->length += 1;

  pthread_mutex_unlock(&s->access_lock);

  sem_post(&s->items_available);

  return 0;
}

int array_get(array *s, char **hostname) {
  sem_wait(&s->items_available);

  pthread_mutex_lock(&s->access_lock);

  char *popped_hostname = s->queue[s->front];
  s->queue[s->front] = NULL;
  s->front = (s->front + 1) % ARRAY_SIZE;
  s->length -= 1;

  pthread_mutex_unlock(&s->access_lock);

  sem_post(&s->space_available);

  *hostname = popped_hostname;
  return 0;
}

void array_free(array *s) {
  sem_destroy(&s->items_available);
  sem_destroy(&s->space_available);
  pthread_mutex_destroy(&s->access_lock);
}

#endif
