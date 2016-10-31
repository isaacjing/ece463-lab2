/*
	ECE 463 Project 2
	Jiangshan Jing, Yifan Wang

*/
#include "ne.h"
#include "router.h"
#include <sys/timerfd.h>

extern struct route_entry routingTable[MAX_ROUTERS];
extern int NumRoutes;
int sock_fd;
FILE *log_file;

void init_router(int argc, char **argv) {
	if (argc < 5) {
		printf("Less than 5 arguments.\n");
		exit(-1);
	}

	int myId = atoi(argv[1]);
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
    struct sockaddr_in serveraddr;
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
    ntoh_pkt_INIT_RESPONSE(&initResponse);

    /* Initialize routing table */
    InitRoutingTbl(&initResponse, myId);
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
	char *alive = (char*)malloc(NumRoutes);
	int i;
	for (i = 0; i < NumRoutes; i++) {
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
	int *neighbor_fds = (int*)malloc(sizeof(int) * NumRoutes);
	for (i = 0; i < NumRoutes; i++) {
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
		for (i = 0; i < NumRoutes; i++)
			if (alive[i])
				FD_SET(neighbor_fds[i], &rfds);
		select(max_fd + 1, &rfds, NULL, NULL, NULL);

		if (FD_ISSET(send_fd, &rfds)) {
		}
		else if (FD_ISSET(sock_fd, &rfds)) {
		}
		else if (FD_ISSET(converge_fd, &rfds)) {
		}
		else {
			for (i = 0; i < NumRoutes; i++) {
				if (FD_ISSET(neighbor_fds[i], &rfds)) {
				}
			}
		}
	}

}

int main (int argc, char **argv) {
	init_router(argc, argv);

	log_file = open_log(argv[1]);

	return 1;
}
