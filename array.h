#include <pthread.h>
#include <semaphore.h>

#define ARRAY_SIZE 8
#define MAX_NAME_LENGTH 255

typedef struct {
    char* queue[ARRAY_SIZE];
    int front;
    int back;
    int length; // keeping track of the length is easier
    pthread_mutex_t access_lock; // put and get can be run at the same time as each other, but not themselves
    sem_t space_available;
    sem_t items_available;
} array;

int  array_init(array *s);                   // initialize the array
int  array_put (array *s, char *hostname);   // place element into the array, block when full
int  array_get (array *s, char **hostname);  // remove element from the array, block when empty
void array_free(array *s);                   // free the array's resources
