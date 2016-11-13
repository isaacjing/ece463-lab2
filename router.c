/*
	ECE 463 Project 2
	Jiangshan Jing, Yifan Wang

*/
#include "ne.h"
#include "router.h"
#include <sys/timerfd.h>
#include <time.h>

extern struct route_entry routingTable[MAX_ROUTERS];
extern int NumRoutes;
int *neighbor_ids;
int sock_fd, myId, NumNeighbors;
FILE *log_file;
struct sockaddr_in serveraddr;
clock_t begin;

void printTable(){
	int i = 0;
	fprintf(log_file, "Routing Table:\n");
	printf("Routing Table:\n");
	for(i = 0; i < NumRoutes; i++){
		fprintf(log_file, "R%d -> R%d: R%d, %d\n", myId, routingTable[i].dest_id, routingTable[i].next_hop
			, routingTable[i].cost);
	}
	fprintf(log_file, "\n");
	fflush(log_file);
}

/*
	Function used to send RT_UPDATE packet to sock_id
	Parameter:
		timer_fd: needs to be reset to UPDATE_INTERVAL in the function
*/
void process_send_updates(int send_fd) {
	int i, err;
	
	struct itimerspec itval;
	
	
	
	for(i = 0; i < NumNeighbors; i++){
		struct pkt_RT_UPDATE updatePackage;
		memset(&updatePackage, 0, sizeof(updatePackage));
		ConvertTabletoPkt(&updatePackage, myId);

		updatePackage.dest_id = neighbor_ids[i];
		//printf("Current dest_id is: %d\n", neighbor_ids[i]);
		//printf("sender_id: %d, dest_id: %d, no_routes: %d\n", updatePackage.sender_id, updatePackage.dest_id, updatePackage.no_routes);
		//printf("first entry dest_id: %d, next_hop: %d, cost: %d\n",
			//updatePackage.route[0].dest_id,
			//updatePackage.route[0].next_hop,
			//updatePackage.route[0].cost);
		hton_pkt_RT_UPDATE(&updatePackage);	//Convert to network endian
		if(sendto(sock_fd, &updatePackage, sizeof(updatePackage), 0, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0){
			fprintf(stderr, "Error when sending updates to ne\n");
			exit(-1);
		}
	}
	//Reset the send_fd
	/*	Initial expiration time */
	itval.it_value.tv_sec = UPDATE_INTERVAL;
	itval.it_value.tv_nsec = 0;

	/*	Periodic expiration time to be 0 
		and update initial expiration time every time it expires*/
	itval.it_interval.tv_sec = 0;
	itval.it_interval.tv_nsec = 0;
	err = timerfd_settime (send_fd, 0, &itval, NULL);
	if (err < 0) {
		fprintf(stderr, "Failed to set time for fd of neighbor %d.", i);
		exit(-1);
	}
}

/*
	Funciton used to receive RT_UPDATE form sock_id
	Update routing table accordingly
	Todo:
		update routing table
		reset converge timer to CONVERGE_TIMEOUT
		re-enable timers for restarted neighbors
		print out to log file
	Parameter:
		neighbor_fds: array of neighbors' fds
		converge_fd: 
*/
void process_receive_updates(int *neighbor_fds, int converge_fd) {
	/* INIT_RESPONSE packet */
    struct pkt_RT_UPDATE pkt_update;
    /* Receive INIT_RESPONSE packet from NE */
    int serverlen = sizeof(serveraddr);
    int n = recvfrom(sock_fd, &pkt_update, sizeof(pkt_update), 0, (struct sockaddr *) &serveraddr, (socklen_t *) &serverlen);
    if (n < 0) {
    	fprintf(stderr, "Failed to receive UPDATES");
    	exit(-1);
    }
    ntoh_pkt_RT_UPDATE(&pkt_update);
    int sender_id = pkt_update.sender_id, num_routes = pkt_update.no_routes;
	printf("SENDER id: %d, num routes: %d\n", sender_id, num_routes);
    int i, j;
    int cost_to_sender;
    int changed = 0;

    for (i = 0; i < NumNeighbors; i++)
    	if (routingTable[i].dest_id == sender_id) {
    		cost_to_sender = routingTable[i].cost;
		printf("cost_to_sender is %d\n", cost_to_sender);
    		break;
    	}

    /*	If cost_to_sender is INIFINITY it just restared */
   	struct itimerspec itval;
   	int err;
    if (cost_to_sender == INFINITY) {
    	int index;
    	for (i = 0; i < NumNeighbors; i++)
    		if (neighbor_ids[i] == sender_id) {
    			index = i;
    			break;
    		}
    	itval.it_value.tv_sec = FAILURE_DETECTION;
    	itval.it_value.tv_nsec = 0;

    	itval.it_interval.tv_sec = 0;
    	itval.it_interval.tv_nsec = 0;
    	err = timerfd_settime(neighbor_fds[index], 0, &itval, NULL);
    	if (err < 0) {
			fprintf(stderr, "Failed to set time for fd of neighbor %d.", i);
			exit(-1);
		}
    }

    for (i = 0; i < num_routes; i++) {
    	struct route_entry entry = pkt_update.route[i];
    	struct route_entry my_entry;

    	int found = 0;
    	for (j = 0; j < NumRoutes; j++)
		printf("Updating entry: %d\n", entry.dest_id);
    		if (routingTable[j].dest_id == entry.dest_id) {
    			found = 1;
    			my_entry = routingTable[j];
			printf("Found entry %d\n", entry.dest_id);
    			break;
    		}

    	/*	If this entry is not present in my routing table, add it */
    	if (!found) {
    		struct route_entry new_entry = entry;
    		new_entry.next_hop = sender_id;
    		new_entry.cost += cost_to_sender;
    		routingTable[NumRoutes++] = new_entry;
    		continue;
    	}

    	/*	Split Horizon Rule */
    	if (entry.next_hop == myId) {
    		continue;
    	}
    	if (entry.cost + cost_to_sender < my_entry.cost) {
    		my_entry.next_hop = sender_id;
    		my_entry.cost = entry.cost + cost_to_sender;
    		changed = 1;
    	}

    	/*	Force Update Rule */
    	if (my_entry.next_hop == sender_id) 
    		if (my_entry.cost != entry.cost + cost_to_sender) {
    			changed = 1;
    			my_entry.cost = entry.cost + cost_to_sender;
    		}
    }
    //printf("Received updates, changed = %d\n", changed);
    if (changed) {
    	/*	Reset converge timer */
    	itval.it_value.tv_sec = CONVERGE_TIMEOUT;
    	itval.it_value.tv_nsec = 0;

    	itval.it_interval.tv_sec = 0;
    	itval.it_interval.tv_nsec = 0;
    	err = timerfd_settime(converge_fd, 0, &itval, NULL);
    	if (err < 0) {
			fprintf(stderr, "Failed to set time for fd of neighbor %d.", i);
			exit(-1);
		}

    	printTable();
    }
}

/*
	Function used to print out to log
	Todo:
		print time elapsed since first receive INIT_RESPONSE
*/
void process_converge(int converge_fd) {
	clock_t end = clock();
	int time_spent = (int)(end - begin) / CLOCKS_PER_SEC;
	fprintf(log_file, "%d: Converged\n", time_spent);
	fflush(log_file);
	struct itimerspec itval;
	itval.it_value.tv_sec = 0;
	itval.it_value.tv_nsec = 0;

	/*	Periodic expiration time to be 0 
		and update initial expiration time every time it expires*/
	itval.it_interval.tv_sec = 0;
	itval.it_interval.tv_nsec = 0;
	int err = timerfd_settime (converge_fd, 0, &itval, NULL);
	if (err < 0) {
		fprintf(stderr, "Failed to set time for fd sending updates.");
		exit(-1);
	}
}


/*
	Function used to process a dead neighbor
	Todo:
		update routing table
		print out to log file
		disable time
	Parameter:

*/
void process_neighbor(int neighbor_fd, int index) {
	UninstallRoutesOnNbrDeath(neighbor_ids[index]);
	printTable();
	struct itimerspec itval;
	int err;

	/*	Initial expiration time */
	itval.it_value.tv_sec = 0;
	itval.it_value.tv_nsec = 0;

	/*	Periodic expiration time to be 0 
		and update initial expiration time every time it expires*/
	itval.it_interval.tv_sec = 0;
	itval.it_interval.tv_nsec = 0;
	err = timerfd_settime (neighbor_fd, 0, &itval, NULL);
	if (err < 0) {
		fprintf(stderr, "Failed to disable dead neighbor timer");
		exit(-1);
	}
}


void init_router(int argc, char **argv) {
	if (argc < 5) {
		printf("Less than 5 arguments.\n");
		exit(-1);
	}

	myId = atoi(argv[1]);
	char *hostname = argv[2];
	int nePort = atoi(argv[3]);
	int myPort = atoi(argv[4]);

	/* socket: create the socket */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "ERROR opening socket");
        exit(-1);
	}

    /* gethostbyname: get the server's DNS entry */
    struct hostent *server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(-1);
    }

    /* build the server's Internet address */
    
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(nePort);

    struct sockaddr_in myAddr;
    bzero((char *) &myAddr, sizeof(myAddr));
    myAddr.sin_family = AF_INET;
    myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myAddr.sin_port = htons(myPort);

    if (bind(sock_fd, (struct sockaddr *) &myAddr, sizeof(myAddr)) < 0) {
    	close(sock_fd);
    	fprintf(stderr, "Failed to bind socket.");
    	exit(-1);
    }

    /* INIT_REQUEST packet */
	struct pkt_INIT_REQUEST initRequest;
	initRequest.router_id = htonl(myId);

    /* Send INIT_REQUEST packet to NE */
    int serverlen = sizeof(serveraddr);
    int n = sendto(sock_fd, &initRequest, sizeof(initRequest), 0, (struct sockaddr *) &serveraddr, serverlen);
    if (n < 0) {
    	fprintf(stderr, "Failed to send INIT_REQUEST");
    	exit(-1);
    }
    
    /* INIT_RESPONSE packet */
    struct pkt_INIT_RESPONSE initResponse;
    /* Receive INIT_RESPONSE packet from NE */
    n = recvfrom(sock_fd, &initResponse, sizeof(initResponse), 0, (struct sockaddr *) &serveraddr, (socklen_t *) &serverlen);
    if (n < 0) {
    	fprintf(stderr, "Failed to receive INIT_RESPONSE");
    	exit(-1);
    }
	
	/* Start the global timer */
	begin = clock();
	
    ntoh_pkt_INIT_RESPONSE(&initResponse);

    /* Initialize routing table */
    InitRoutingTbl(&initResponse, myId);

    /* Record router_ids of each neighbor */
    neighbor_ids = (int*)malloc(sizeof(int) * (NumRoutes - 1));
    NumNeighbors = NumRoutes - 1;
    int i;
    for (i = 0; i < NumRoutes; i++) 
    	neighbor_ids[i] = routingTable[i].dest_id;
}

FILE *open_log(char *myId) {
	char fileName[100];
	strcpy(fileName, "router");
	strcat(fileName, myId);
	strcat(fileName, ".log");
	return fopen(fileName, "wr");
}

void main_loop() {
	/*	If alive[id] is 0, router<id> is dead 
		first assume all neighbors are alive */
	char *alive = (char*)malloc(NumNeighbors);
	int i;
	for (i = 0; i < NumNeighbors; i++) {
		alive[i] = 1;
	}

	/*	Timer for sending RT_UPDATE */
	int send_fd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (send_fd < 0) {
		fprintf(stderr, "Failed to create fd for sending updates.");
		exit(-1);
	}

	int max_fd = send_fd;
	struct itimerspec itval;
	int err;

	/*	Initial expiration time */
	itval.it_value.tv_sec = UPDATE_INTERVAL;
	itval.it_value.tv_nsec = 0;

	/*	Periodic expiration time to be 0 
		and update initial expiration time every time it expires*/
	itval.it_interval.tv_sec = 0;
	itval.it_interval.tv_nsec = 0;
	err = timerfd_settime (send_fd, 0, &itval, NULL);
	if (err < 0) {
		fprintf(stderr, "Failed to set time for fd sending updates.");
		exit(-1);
	}

	/*	Timers for detecting dead neighbors */
	int *neighbor_fds = (int*)malloc(sizeof(int) * (NumNeighbors));
	for (i = 0; i < NumNeighbors; i++) {
		neighbor_fds[i] = timerfd_create(CLOCK_MONOTONIC, 0);
		if (neighbor_fds[i] < 0) {
			fprintf(stderr, "Failed to create fd for neighbor %d.", i);
			exit(-1);
		}
		max_fd = neighbor_fds[i] > max_fd ? neighbor_fds[i] : max_fd;

		/*	Initial expiration time */
		itval.it_value.tv_sec = FAILURE_DETECTION;
		itval.it_value.tv_nsec = 0;
		/*	Periodic expiration time to be 0 
		and update initial expiration time every time it expires*/
		itval.it_interval.tv_sec = 0;
		itval.it_interval.tv_nsec = 0;
		err = timerfd_settime (neighbor_fds[i], 0, &itval, NULL);
		if (err < 0) {
			fprintf(stderr, "Failed to set time for fd of neighbor %d.", i);
			exit(-1);
		}
	}

	/*	Timer for detecting convergence */
	int converge_fd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (converge_fd < 0) {
		fprintf(stderr, "Failed to create fd for sending updates.");
		exit(-1);
	}	
	max_fd = converge_fd > max_fd ? converge_fd : max_fd;

	/*	Initial expiration time */
	itval.it_value.tv_sec = CONVERGE_TIMEOUT;
	itval.it_value.tv_nsec = 0;

	/*	Periodic expiration time to be 0 
		and update initial expiration time every time it expires*/
	itval.it_interval.tv_sec = 0;
	itval.it_interval.tv_nsec = 0;
	err = timerfd_settime (converge_fd, 0, &itval, NULL);
	if (err < 0) {
		fprintf(stderr, "Failed to set time for fd sending updates.");
		exit(-1);
	}

	max_fd = sock_fd > max_fd ? sock_fd : max_fd;
	

	/*	Loop for various timers */
	fd_set rfds;
	while (1) {
		FD_ZERO(&rfds);
		FD_SET(sock_fd, &rfds);
		FD_SET(send_fd, &rfds);
		FD_SET(converge_fd, &rfds);
		for (i = 0; i < NumNeighbors; i++)
			if (alive[i])
				FD_SET(neighbor_fds[i], &rfds);
		select(max_fd + 1, &rfds, NULL, NULL, NULL);

		if (FD_ISSET(send_fd, &rfds)) {
			process_send_updates(send_fd);
		}
		else if (FD_ISSET(sock_fd, &rfds)) {
			
			process_receive_updates(neighbor_fds, converge_fd);
		}
		else if (FD_ISSET(converge_fd, &rfds)) {
			process_converge(converge_fd);
		}
		else {
			for (i = 0; i < NumNeighbors; i++) {
				if (FD_ISSET(neighbor_fds[i], &rfds)) {
					process_neighbor(neighbor_fds[i], i);
				}
			}
		}
	}

}

int main (int argc, char **argv) {
	init_router(argc, argv);

	log_file = open_log(argv[1]);
	main_loop();
	fclose(log_file);
	return 1;
}
