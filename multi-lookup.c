#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include "array.h"
#include "util.h"

#define MAX_FILES 100
#define MAX_REQUESTER_THREADS 10
#define MAX_RESOLVER_THREADS 10
#define MAX_IP_LENGTH INET6_ADDRSTRLEN

struct request_arg_struct {
  array *hostname_array;

  pthread_mutex_t *get_name_file_mutex;
  char ** argv;
  int name_file_count;
  int *total_serviced_files_count;


  pthread_mutex_t *requester_log_mutex;
  FILE *requester_log_file;
};

struct resolve_arg_struct {
  array *hostname_array;

  pthread_mutex_t *resolver_log_mutex;
  FILE *resolver_log_file;

  // pointer to the poison pill
  char *poison_pill;
};

void *request(void *request_args) {
  struct request_arg_struct *args = (struct request_arg_struct*)request_args;

  int thread_serviced_files_count = 0;

  while (1) {
    pthread_mutex_lock(args->get_name_file_mutex);
    if (args->name_file_count <= *args->total_serviced_files_count) {
      pthread_mutex_unlock(args->get_name_file_mutex);
      break;
    }
    char *data_filename = args->argv[5 + *args->total_serviced_files_count];
    *args->total_serviced_files_count += 1;
    pthread_mutex_unlock(args->get_name_file_mutex);

    FILE *file = fopen(data_filename, "r");
    if (file == NULL) {
      fprintf(stderr, "invalid file %s\n", data_filename);
      continue;
    }

    while(1) {
      char *hostname_buffer = malloc(MAX_NAME_LENGTH);
      if (!fgets(hostname_buffer, MAX_NAME_LENGTH, file)) {
        free(hostname_buffer);
        break;
      }

      // remove newline from buffer
      hostname_buffer[strcspn(hostname_buffer, "\n")] = 0;

      array_put(args->hostname_array, hostname_buffer);

      pthread_mutex_lock(args->requester_log_mutex);
      fprintf(args->requester_log_file, "%s\n", hostname_buffer);
      fflush(args->requester_log_file);
      pthread_mutex_unlock(args->requester_log_mutex);
    }

    fclose(file);

    thread_serviced_files_count += 1;
  }

  printf("thread %lu serviced %d files\n", pthread_self(), thread_serviced_files_count);
  return NULL;
}

void *resolve(void *resolver_args) {
  struct resolve_arg_struct *args = (struct resolve_arg_struct*)resolver_args;

  int thread_resolved_files_count = 0;

  while (1) {
    char *hostname;
    array_get(args->hostname_array, &hostname);

    if (hostname == args->poison_pill) {
      break;
    }

    char address_buffer[MAX_IP_LENGTH] = "NOT RESOLVED";
    dnslookup(hostname, address_buffer, MAX_IP_LENGTH);

    pthread_mutex_lock(args->resolver_log_mutex);
    fprintf(args->resolver_log_file, "%s, %s\n", hostname, address_buffer);
    fflush(args->resolver_log_file);
    pthread_mutex_unlock(args->resolver_log_mutex);

    thread_resolved_files_count++;
    free(hostname);
  }

  printf("thread %lu resolved %d hostnames\n", pthread_self(), thread_resolved_files_count);
  return NULL;
}

int main(int argc, char **argv) {
  if (argc <= 4) {
    printf("NAME\nmulti-lookup - resolve a set of hostnames to IP addresses\n\nSYNOPSIS\nmulti-lookup <# requester> <# resolver> <requester log> <resolver log> [ <data file> ... ]\n\nDESCRIPTION\nThe file names specified by <data file> are passed to the pool of requester threads which place information into a shared data area. Resolver threads read the shared data area and find the corresponding IP address.\n\n<# requesters> number of requestor threads to place into the thread pool\n<# resolvers> number of resolver threads to place into the thread pool\n<requester log> name of the file into which requested hostnames are written\n<resolver log> name of the file into which hostnames and resolved IP addresses are written\n<data file> filename to be processed. Each file contains a list of host names, one per line, that are to be resolved\n");
    exit(UTIL_FAILURE);
  }

  int name_file_count = argc - 5;
  if (name_file_count > MAX_FILES) {
    fprintf(stderr, "Too many input files! The maximum is %d but %d were given", MAX_FILES, name_file_count);
    exit(UTIL_FAILURE);
  }

  int requester_thread_count = atoi(argv[1]);
  if (requester_thread_count > MAX_REQUESTER_THREADS || requester_thread_count < 0) {
    fprintf(stderr, "invalid number of requester threads! The maximum is %d", MAX_REQUESTER_THREADS);
    exit(UTIL_FAILURE);
  }

  int resolver_thread_count = atoi(argv[2]);
  if (resolver_thread_count > MAX_RESOLVER_THREADS || requester_thread_count < 0) {
    fprintf(stderr, "invalid number of resolver threads! The maximum is %d", MAX_RESOLVER_THREADS);
    exit(UTIL_FAILURE);
  }

  char *requester_log_filename = argv[3];
  char *resolver_log_filename = argv[4];

  FILE *requester_log = fopen(requester_log_filename, "w");
  if (requester_log == NULL) {
    fprintf(stderr, "failed to open requester_log file %s", requester_log_filename);
    exit(UTIL_FAILURE);
  }

  FILE *resolver_log = fopen(resolver_log_filename, "w");
  if (resolver_log == NULL) {
    fprintf(stderr, "failed to open resolver_log file %s", resolver_log_filename);
    exit(UTIL_FAILURE);
  }

  struct timeval start_time;
  struct timeval end_time;
  gettimeofday(&start_time, NULL);

  array hostname_array;
  array_init(&hostname_array);

  int serviced_files_count = 0;
  char *poison_pill = malloc(MAX_IP_LENGTH);

  // mutex for accessing input files (argv)
  pthread_mutex_t get_input_files_mutex;
  // mutex for writing to the requester log
  pthread_mutex_t requester_log_mutex;
  // mutex for writing to the resolver log
  pthread_mutex_t resolver_log_mutex;

  pthread_mutex_init(&get_input_files_mutex, NULL);
  pthread_mutex_init(&requester_log_mutex, NULL);
  pthread_mutex_init(&resolver_log_mutex, NULL);

  pthread_t requester_threads[requester_thread_count];
  pthread_t resolver_threads[resolver_thread_count];

  // Create requester threads
  struct request_arg_struct request_args = { &hostname_array, &get_input_files_mutex, argv, name_file_count, &serviced_files_count, &requester_log_mutex, requester_log };
  struct resolve_arg_struct resolve_args = { &hostname_array, &resolver_log_mutex, resolver_log, poison_pill };

  for (int i = 0; i < requester_thread_count; i++) {
    pthread_create(&requester_threads[i], NULL, request, &request_args);
  }

  // Create resolver threads
  for (int i = 0; i < resolver_thread_count; i++) {
    pthread_create(&resolver_threads[i], NULL, resolve, &resolve_args);
  }

  // wait for all threads to finish
  for (int i = 0; i < requester_thread_count; i++) {
    pthread_join(requester_threads[i], NULL);
  }

  // use poison pill
  for (int i = 0; i < resolver_thread_count; i++) {
    array_put(&hostname_array, poison_pill);
  }

  // Create resolver threads
  for (int i = 0; i < resolver_thread_count; i++) {
    pthread_join(resolver_threads[i], NULL);
  }


  gettimeofday(&end_time, NULL);
  double elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
  printf("%s: total time is %f seconds\n", argv[0], elapsed);

  free(poison_pill);

  fclose(requester_log);
  fclose(resolver_log);
  array_free(&hostname_array);
  pthread_mutex_destroy(&get_input_files_mutex);
  pthread_mutex_destroy(&requester_log_mutex);
  pthread_mutex_destroy(&resolver_log_mutex);
}
