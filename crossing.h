#ifndef CROSSING_H
#define CROSSING_H
#include <time.h>
#include <semaphore.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include "csapp.h"

enum thread_type_t {
    PEDESTRIAN = 0,
    VEHICLE = 1
} typedef thread_type_t;

/* A superstruct made to keep track of thread type and number */
struct thread_info {
    struct info* info_str;
    int thread_nr;
    int crossing;
    thread_type_t type;
} typedef thread_info;

int tail[4];
sem_t queue_mtx;
int walk_time;
int drive_time;


int start_wait(thread_info *info);

int pedestrian_arrive(thread_info *info);
void pedestrian_walk(thread_info *info);
void pedestrian_leave(thread_info *info);

int vehicle_arrive(thread_info *info);
void vehicle_drive(thread_info *info);
void vehicle_leave(thread_info *info);

void rand_sleep(int ms);
void cross_init(int num_vehicles, int num_pedestrians);

#endif /* CROSSING_H */
