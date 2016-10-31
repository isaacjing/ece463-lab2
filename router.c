/*
	ECE 463 Project 2
	Jiangshan Jing, Yifan Wang

*/
#include "ne.h"
#include "router.h"

extern struct route_entry routingTable[MAX_ROUTERS];

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
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
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

    if (bind(sockfd, (struct sockaddr *) &myAddr, sizeof(myAddr)) < 0) {
    	close(sockfd);
    	fprintf(stderr, "Failed to bind socket.");
    	exit(-1);
    }

    /* INIT_REQUEST packet */
	struct pkt_INIT_REQUEST initRequest;
	initRequest.router_id = htonl(myId);

    /* Send INIT_REQUEST packet to NE */
    int serverlen = sizeof(serveraddr);
    int n = sendto(sockfd, &initRequest, sizeof(initRequest), 0, (struct sockaddr *) &serveraddr, serverlen);
    if (n < 0) {
    	fprintf(stderr, "Failed to send INIT_REQUEST");
    	exit(-1);
    }
    
    /* INIT_RESPONSE packet */
    struct pkt_INIT_RESPONSE initResponse;
    /* Receive INIT_RESPONSE packet from NE */
    n = recvfrom(sockfd, &initResponse, sizeof(initResponse), 0, (struct sockaddr *) &serveraddr, (socklen_t *) &serverlen);
    if (n < 0) {
    	fprintf(stderr, "Failed to receive INIT_RESPONSE");
    	exit(-1);
    }
    ntoh_pkt_INIT_RESPONSE(&initResponse);

    InitRoutingTbl(&initResponse, myId);
}

int main (int argc, char **argv) {
	init_router(argc, argv);

	char fileName[100];
	strcpy(fileName, "router");
	strcat(fileName, argv[1]);
	strcat(fileName, ".log");
	FILE *log_file = fopen(fileName, "wr");
	return 1;
}
