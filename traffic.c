#include <stdbool.h>
#include <assert.h>
#include "crossing.h"

/*
 * In this assignment we are to simulate an intersection and make sure no one runs anyone over or crashes and
 * no one has to wait too long.
 * We use a separate controller thread to control the others.
 * Each thread (pedestrian or vehicle) has its own personal semaphore that the controller uses to allow them
 * to cross. Furthermore we use semaphores to count the amount of threads waiting and which threads have arrived.
 * We use an array of size 4 (k_wait) to keep count of when a thread has waited too long. (kfairness)
 * Some helper functions were made in hope of making the code more readable.
 *
 * Enjoy the read.
 */

/*
 * The possible directions (crossings) of threads
 */
enum direction {
    PED_VER = 0,
    VEH_VER = 1,
    PED_HOR = 2,
    VEH_HOR = 3
} typedef direction;


/*
 * This struct allows us to keep the ticket of the thread in info_str
 */
struct info {
    int ticket;
};

/*
 * These have already been defined. You should know this, you wrote it yourself.
 */
int K;
int num_vehicles;
int num_pedestrians;

thread_info **ticket_to_thread; // Array of thread_info pointers. Accessible by ticket number

pthread_t *ticket_threads; // Array of threads. Used for joining the threads in the end
pthread_t gatekeeper; // The gatekeeper thread. Used to join the gatekeeper in the end

sem_t *ticket_semaphores; // Array of binary semaphores that each thread waits for before starting to cross
sem_t waiting; // Counter semaphore that tells the controller whether any thread is waiting to be allowed to cross.
				// The Controller can sleep while this is 0.
sem_t *ticket_arrived; // Array of binary semaphores that the controller uses to know which threads have arrived
						// and waiting to be allowed to cross.
sem_t ticket_mutex; // Mutex for the ticket dispenser
sem_t *tickets_crossing_mutex; // One mutex for each direction to be able to keep count of how many tickets are crossing
sem_t *tickets_waiting_mutex; // One mutex for each direction to be able to keep count of how many tickets are waiting
int *tickets_crossing; // One int for each direction that keeps count of how many tickets are crossing in that direction
int *tickets_waiting; // One int for each direction that keeps count of how many tickets are waiting in each direction

int next_ticket = 0; // initialised ticket dispenser

void *pedestrians(void *arg);
void *vehicles(void *arg);
void *traffic_controller(void *arg);


/*
 * Initialises the traffic controller.
 * Arrays are malloc'd
 * Semaphores are initialised
 * The Gatekeeper thread is created
 */
void init()
{
    int i;
    sem_init(&ticket_mutex, 0, 1);
	sem_init(&waiting, 0, 0);

    ticket_threads = malloc(sizeof(pthread_t) * (num_vehicles + num_pedestrians));
    
    ticket_semaphores = malloc(sizeof(sem_t) * (num_vehicles + num_pedestrians));

    ticket_arrived = malloc(sizeof(sem_t) * (num_vehicles + num_pedestrians));
    ticket_to_thread = malloc(sizeof(thread_info*) * (num_vehicles + num_pedestrians));
    
	tickets_crossing_mutex = malloc(sizeof(sem_t) * 4);
	tickets_waiting_mutex = malloc(sizeof(sem_t) * 4);
	tickets_crossing = malloc(sizeof(int) * 4);
	tickets_waiting = malloc(sizeof(int) * 4);

	/*
	 * In each of the 4 directions no one is waiting or crossing
	 */
	for (i = 0; i < 4 ; i++) {
		sem_init(&tickets_crossing_mutex[i], 0, 1);
		sem_init(&tickets_waiting_mutex[i], 0, 1);
		tickets_crossing[i] = 0;
		tickets_waiting[i] = 0;
	}

    for(i = 0; i < num_vehicles + num_pedestrians; i++) {
        sem_init(&ticket_semaphores[i], 0, 0); //can this ticket cross?
        sem_init(&ticket_arrived[i], 0, 0); //has this ticket arrived?
    } 

	if(pthread_create(&gatekeeper, 0, traffic_controller, 0)) {
		fprintf(stderr, "Error creating gatekeeper thread.\n");
		return;
	}
}

/*
 * Safely updates the ticket_dispenser and returns the ticket that was dispensed
 */
int take_ticket(void)
{
	P(&ticket_mutex);
	int ticket = next_ticket;
	next_ticket++;
	V(&ticket_mutex);
	return ticket;
}


/*
 * Adds ticket to arg->info_str->ticket so that it can be accessed by the thread at all times
 */
void add_ticket_to_thread(thread_info *arg, int ticket)
{
	struct info* info = malloc(sizeof(struct info));
	info->ticket = ticket;
	arg->info_str = info;
}

/*
 * Updates semaphores and counters when a thread arrives
 */
void notify_of_arrival(int ticket, int crossing)
{
	V(&waiting); // One more ticket is waiting (If the controller was sleeping, WAKE UP AND HELP ME CROSS)
	V(&ticket_arrived[ticket]); // More precisely *I* thread nr ticket have arrived AND WANT TO PASS

	// *I* am a thread and I am waiting to be able to cross ind direction crossing
	P(&tickets_waiting_mutex[crossing]); 
	tickets_waiting[crossing]++;
	V(&tickets_waiting_mutex[crossing]);
}

/*
 * Updates semaphores and counters when a thread has finished crossing
 */
void notify_of_ending(int ticket, int crossing)
{
	// Alright *I*'m not crossing in direction crossing anymore. Maybe even, no one is!
	P(&tickets_crossing_mutex[crossing]);
	tickets_crossing[crossing]--;
	V(&tickets_crossing_mutex[crossing]);
}

/*
 * Get's a ticket
 * Updates ticket_to_thread (So we can fid threads by their ticket number)
 * Adds ticket to thread_info
 * Creates the proper thread
 * Notifies of the ticket's arrival
 */
void spawn_pedestrian(thread_info *arg)
{
    int ticket = take_ticket();
    ticket_to_thread[ticket] = arg;
    
	add_ticket_to_thread(arg, ticket);

	if(pthread_create(&ticket_threads[ticket], 0, pedestrians, arg)) {
		fprintf(stderr, "Error creating pedestrian thread.\n");
		return;
	}

	notify_of_arrival(ticket, arg->crossing);
}

/*
 * Same as above, but for a vehicle :)))))
 */
void spawn_vehicle(thread_info *arg)
{
    int ticket = take_ticket();
	ticket_to_thread[ticket] = arg;
    
	add_ticket_to_thread(arg, ticket);
	
	if(pthread_create(&ticket_threads[ticket], 0, vehicles, arg)) {
		fprintf(stderr, "Error creating vehicle thread.\n");
		return;
	}

	notify_of_arrival(ticket, arg->crossing);
}


/*
 * The main procedure for vehicles
 * 
 * Stores the thread info in info
 * Calls vehicle arrive
 * Waits for a personal green light
 * Drives and Leaves when it can
 * Notifies that it has crossed safely
 */
void *vehicles(void *arg)
{
    thread_info *info = arg;

    // Each thread needs to call these functions in this order
    // Note that the calls can also be made in helper functions
    int place = vehicle_arrive(info);
    P(&ticket_semaphores[info->info_str->ticket]);
    vehicle_drive(info);
    vehicle_leave(info);
    
	notify_of_ending(info->info_str->ticket, info->crossing);
	
	return NULL;
}

/*
 * Same as above but for Pedestrians (Why is this even different? The only thing that differs is the "crossing" variable)
 */
void *pedestrians(void *arg)
{
    thread_info *info = arg;

    // Each thread needs to call these functions in this order
    // Note that the calls can also be made in helper functions
    int place = pedestrian_arrive(info);
    P(&ticket_semaphores[info->info_str->ticket]);
    pedestrian_walk(info);
    pedestrian_leave(info);
    
	notify_of_ending(info->info_str->ticket, info->crossing);
	
	return NULL;
}

/*
 * Returns the first ticket that has not been allowed to cross.
 * If all tickets have been allowed to cross, returns -1. This notifies the controller to stop its idle loop
 */
int first_not_sent(int* ticket_sent, int size) {
	int i;
	for (i = 0; i < size; i++) {
		if (ticket_sent[i] == 0) {
			return i;
		}
	}
	return -1;
}

/*
 * Checks wether threads going in direction crossing are compatible with the ones that are already crossing.
 *
 * Consists of 4 blocks, one for each direction:
 *
 * take crossing mutex for the direction that is *not* compatible with crossing.
 * Checks if there are any threads crossing in that direction.
 * Returns the mutex
 * If anyone that is *not* compatible (If they would collide) returns 0, otherwise 1
 */
int is_compatible(int crossing) {
	if (crossing == PED_VER) {
		P(&tickets_crossing_mutex[VEH_HOR]);
		if (tickets_crossing[VEH_HOR] > 0) {
			V(&tickets_crossing_mutex[VEH_HOR]);
			return 0;
		} else {
			V(&tickets_crossing_mutex[VEH_HOR]);
			return 1;
		}
	}
	
	else if (crossing == PED_HOR) {
		P(&tickets_crossing_mutex[VEH_VER]);
		if (tickets_crossing[VEH_VER] > 0) {
			V(&tickets_crossing_mutex[VEH_VER]);
			return 0;
		} else {
			V(&tickets_crossing_mutex[VEH_VER]);
			return 1;
		}
	}
	
	else if (crossing == VEH_VER) {
		P(&tickets_crossing_mutex[VEH_HOR]);
		if (tickets_crossing[VEH_HOR] > 0) {
			V(&tickets_crossing_mutex[VEH_HOR]);
			return 0;
		}
		V(&tickets_crossing_mutex[VEH_HOR]);

		P(&tickets_crossing_mutex[PED_HOR]);
		if (tickets_crossing[PED_HOR] > 0) {
			V(&tickets_crossing_mutex[PED_HOR]);
			return 0;
		}
		V(&tickets_crossing_mutex[PED_HOR]);
		return 1;
	}
	
	else {
		P(&tickets_crossing_mutex[VEH_VER]);
		if (tickets_crossing[VEH_VER] > 0) {
			V(&tickets_crossing_mutex[VEH_VER]);
			return 0;
		}
		V(&tickets_crossing_mutex[VEH_VER]);

		P(&tickets_crossing_mutex[PED_VER]);
		if (tickets_crossing[PED_VER] > 0) {
			V(&tickets_crossing_mutex[PED_VER]);
			return 0;
		}
		V(&tickets_crossing_mutex[PED_VER]);
	}
	return 1;
}

/*
 * Updates the k_wait when a ticket is sent in direction crossing
 *
 * Consists of 4 blocks, one for each direction:
 *
 * Takes the waiting mutex for the direction that possibly gets frustrated
 * If there is someone waiting to cross in that direction:
 *		up one the amount of threads the possibly frustrated direction has had to wait
 * Otherwise:
 *		If no one is waiting there's no one to get frustrated ;)
 * Then reinitialises the direction that got sent to 0, since it has just been sent and the next one has not waited for anyone
 */
void kfairness_send(int crossing, int *k_wait) {
	if (crossing == PED_HOR) {
		P(&tickets_waiting_mutex[VEH_VER]);
		if (tickets_waiting[VEH_VER] > 0){
			k_wait[VEH_VER]++;
		}
		V(&tickets_waiting_mutex[VEH_VER]);
		k_wait[PED_HOR] = 0;
	}
	
	else if (crossing == PED_VER) {
		P(&tickets_waiting_mutex[VEH_HOR]);
		if (tickets_waiting[VEH_HOR] > 0){
			k_wait[VEH_HOR]++;
		}
		V(&tickets_waiting_mutex[VEH_HOR]);
		k_wait[PED_VER] = 0;
	}
	
	else if (crossing == VEH_HOR) {
		P(&tickets_waiting_mutex[PED_VER]);
		if (tickets_waiting[PED_VER] > 0){
			k_wait[PED_VER]++;
		}
		V(&tickets_waiting_mutex[PED_VER]);
		k_wait[VEH_HOR] = 0;
	}
	
	else {
		P(&tickets_waiting_mutex[PED_HOR]);
		if (tickets_waiting[PED_HOR] > 0){
			k_wait[PED_HOR]++;
		}
		V(&tickets_waiting_mutex[PED_HOR]);
		k_wait[VEH_VER] = 0;
	}
}

/*
 * Checks whether sending a thread in direction crossing will drive someone else nuts (Hae to wait for more than K threads)
 *
 * Cosnists of 4 blocks, one for each possible direction:
 *
 * Checks if the possibly frustrated threads have waited for K other threads (and whether there is anyone waiting)
 * If no one is waiting or they can take one more thread passing by without going crazy, return 1.
 * If the other thread is about to go nuts return 0, all the threads should be considerate of each other.
 */
int kfairness_check(int crossing, int*k_wait) {
	if (crossing == PED_HOR) {
		P(&tickets_waiting_mutex[VEH_VER]);
		if (k_wait[VEH_VER] == K && tickets_waiting[VEH_VER] > 0) {
			V(&tickets_waiting_mutex[VEH_VER]);
			return 0;
		}
		V(&tickets_waiting_mutex[VEH_VER]);
	}
	
	else if (crossing == PED_VER) {
		P(&tickets_waiting_mutex[VEH_HOR]);
		if (k_wait[VEH_HOR] == K && tickets_waiting[VEH_HOR] > 0) {
			V(&tickets_waiting_mutex[VEH_HOR]);
			return 0;
		}
		V(&tickets_waiting_mutex[VEH_HOR]);
	}
	
	else if (crossing == VEH_HOR) {
		P(&tickets_waiting_mutex[PED_VER]);
		if (k_wait[PED_VER] == K && tickets_waiting[PED_VER] > 0) {
			V(&tickets_waiting_mutex[PED_VER]);
			return 0;
		}
		V(&tickets_waiting_mutex[PED_VER]);
	}
	
	else {
		P(&tickets_waiting_mutex[PED_HOR]);
		if (k_wait[PED_HOR] == K && tickets_waiting[PED_HOR] > 0) {
			V(&tickets_waiting_mutex[PED_HOR]);
			return 0;
		}
		V(&tickets_waiting_mutex[PED_HOR]);
	}
	return 1;
}

void *traffic_controller(void *arg)
{
    int i, j, value, next; // integers that will be used (counter, counter, semaphore_value, next_ticket)
	int *ticket_sent = malloc(sizeof(int) * (num_pedestrians + num_vehicles)); // Array that holds which tickets have been sent on their way
	int *k_wait = malloc(sizeof(int) * 4); // Array that holds how many threads have passed each direction (to be able to see when they are about to go nuts due to K-fairness)

	//initialising
	for (i = 0; i < num_vehicles + num_pedestrians; i ++){
		ticket_sent[i] = 0;
	}

	//initialising
	for (i = 0; i < 4; i++) {
		k_wait[i] = 0;
	}
	
	// While some ticket has not been sent
	while((next = first_not_sent(ticket_sent, num_vehicles + num_pedestrians)) > -1) {
		P(&waiting); // Sleep until some ticket is waiting
		V(&waiting); // Put it back to maintain the number of waiting tickets
		
		thread_info* next_info = ticket_to_thread[next]; // Get the thread info for quick reference

		// Find a thread that is both compatible, fair to the other threads and has not been sent yet.
		// Also there must actually be a thread that will have that ticket
		while ((!kfairness_check(next_info->crossing, k_wait) || !is_compatible(next_info->crossing) || ticket_sent[next] == 1) && next < num_vehicles + num_pedestrians - 1){
			next++;
			sem_getvalue(&ticket_arrived[next], &value); // If this thread has not arrived, stop checking
			if (value == 0) {
				break;
			}
			next_info = ticket_to_thread[next];
		}
		
		// For each thread after next check the conditions in the if (Read a bit further)
		for (j = next; j < num_vehicles + num_pedestrians; j++) {
			sem_getvalue(&ticket_arrived[j], &value); // Again, if the thread has not arrived, stop checking
			if (value == 0) break;

			// Check if the thread has not been sent and is going in the same direction as crossing and will not drive anyone crazy on k fairness
			if (ticket_sent[j] == 0 && ticket_to_thread[j]->crossing == next_info->crossing && kfairness_check(next_info->crossing, k_wait)) {
				P(&tickets_crossing_mutex[next_info->crossing]); // Safely up the number of threads that are crossing in the direction of next
				tickets_crossing[next_info->crossing]++;
				V(&tickets_crossing_mutex[next_info->crossing]);
				V(&ticket_semaphores[j]); // allow j to cross
				ticket_sent[j] = 1; // Bookkeeping. j has been sent DON'T SEND IT AGAIN
				P(&waiting); // There's one less tickets waiting
				P(&tickets_waiting_mutex[next_info->crossing]); // Safely down the number of tickets that are waiting to cross in the direction of next
				tickets_waiting[next_info->crossing]--;
				V(&tickets_waiting_mutex[next_info->crossing]);
				kfairness_send(next_info->crossing, k_wait); // Update the kfairness properly
			}
		}
	}

	free(ticket_sent); //Free memory
	free(k_wait);

    return NULL;
}

/*
 * Join all the threads and free all the appropriate memory
 */
void clean()
{
    int i;
	// Join all the crossing threads
    for(i = 0; i < num_pedestrians + num_vehicles; i++) {
		if(pthread_join(ticket_threads[i], NULL)) {
			fprintf(stderr, "Error joining thread.\n");
			return;
		}
	}

	// Join the Gatekeeper thread
	if(pthread_join(gatekeeper, NULL)) {
		fprintf(stderr, "Error joining gatekeeper thread.\n");
		return;
	}

	// FREE ALL THE MEMORY
    free(ticket_threads);
    free(ticket_semaphores);
    for(i = 0; i < num_vehicles + num_pedestrians; i++) {
		free(ticket_to_thread[i]->info_str);
        free(ticket_to_thread[i]);
    }
    free(ticket_to_thread);
    free(ticket_arrived);
	free(tickets_crossing);
	free(tickets_waiting);
}

// Thank you for reading
