/*
	ECE 463 Project 2
	Jiangshan Jing, Yifan Wang

*/
#include "ne.h"
#include "router.h"


struct route_entry routingTable[MAX_ROUTERS];	//This is the routing table
int NumRoutes;	//Number of routers in routing table


/* Routine Name    : InitRoutingTbl
 * INPUT ARGUMENTS : 1. (struct pkt_INIT_RESPONSE *) - The INIT_RESPONSE from Network Emulator
 *                   2. int - My router's id received from command line argument.
 * RETURN VALUE    : void
 * USAGE           : This routine is called after receiving the INIT_RESPONSE message from the Network Emulator. 
 *                   It initializes the routing table with the bootstrap neighbor information in INIT_RESPONSE.  
 *                   Also sets up a route to itself (self-route) with next_hop as itself and cost as 0.
 */
 

void InitRoutingTbl (struct pkt_INIT_RESPONSE *InitResponse, int myID){

	int i;
	struct nbr_cost neighborCost;
	
	for(i = 0; i < InitResponse->no_nbr; i++){
		neighborCost = InitResponse->nbrcost[i];
		routingTable[i].cost = neighborCost.cost;
		routingTable[i].next_hop = neighborCost.nbr;
		routingTable[i].dest_id = neighborCost.nbr;
	}
	routingTable[i].cost = 0;
	routingTable[i].next_hop = myID;
	routingTable[i].dest_id = myID;
	NumRoutes = i + 1;
	//printf("NumRoutes = %d\n", NumRoutes);
}

/* Routine Name    : UpdateRoutes
 * INPUT ARGUMENTS : 1. (struct pkt_RT_UPDATE *) - The Route Update message from one of the neighbors of the router.
 *                   2. int - The direct cost to the neighbor who sent the update. 
 *                   3. int - My router's id received from command line argument.
 * RETURN VALUE    : int - Return 1 : if the routing table has changed on running the function.
 *                         Return 0 : Otherwise.
 * USAGE           : This routine is called after receiving the route update from any neighbor. 
 *                   The routing table is then updated after running the distance vector protocol. 
 *                   It installs any new route received, that is previously unknown. For known routes, 
 *                   it finds the shortest path using current cost and received cost. 
 *                   It also implements the forced update and split horizon rules. My router's id
 *                   that is passed as argument may be useful in applying split horizon rule.
 */
int UpdateRoutes(struct pkt_RT_UPDATE *RecvdUpdatePacket, int costToNbr, int myID){
	int i = 0;
	int j = 0;
	unsigned int newCost = 0;
	int changed = 0;
	struct route_entry currentEntry;
	while (i < RecvdUpdatePacket->no_routes){		//Outer loop traverse the received updates
		currentEntry = RecvdUpdatePacket->route[i];
		newCost = currentEntry.cost + costToNbr;
		if(newCost > INFINITY){
			newCost = INFINITY;
		}
		while(j < NumRoutes){						//Inner loop traverse current routing table
			if(routingTable[j].dest_id == currentEntry.dest_id){	//If Destination ID matches
				//printf("newCost = %d, routingTable[j].cost = %d, currentEntry.next_hop = %d, myID = %d\n", newCost, routingTable[j].cost, currentEntry.next_hop, myID);
				if (newCost < routingTable[j].cost && currentEntry.next_hop != myID){	
					//If new cost is smaller and neighbor's next hop is not me
					routingTable[j].next_hop = RecvdUpdatePacket->sender_id;
					routingTable[j].cost = newCost;
					changed = 1;
				}else if(routingTable[j].next_hop == RecvdUpdatePacket->sender_id && routingTable[j].cost != newCost){//Forced update rule
					//If my next_hop is neighbor, also only update if new cost is different than current cost
					routingTable[j].cost = newCost;
					changed = 1;
				}
				break;	//once destination matches, no need to continue for current destination.
			}
			j++;
		}
		if(j == NumRoutes){	//Not found the updates destination in my routing table
			routingTable[j].dest_id = currentEntry.dest_id;
			routingTable[j].next_hop = RecvdUpdatePacket->sender_id;
			//printf("NumRoutes = %d, MAX_ROUTERS = %d, j = %d, newCost = %d\n", NumRoutes, MAX_ROUTERS, j, newCost);
			routingTable[j].cost = newCost;
			NumRoutes += 1;
			changed = 1;
		}
		i++;
	}
	return changed;
}



/* Routine Name    : ConvertTabletoPkt
 * INPUT ARGUMENTS : 1. (struct pkt_RT_UPDATE *) - An empty pkt_RT_UPDATE structure
 *                   2. int - My router's id received from command line argument.
 * RETURN VALUE    : void
 * USAGE           : This routine fills the routing table into the empty struct pkt_RT_UPDATE. 
 *                   My router's id  is copied to the sender_id in pkt_RT_UPDATE. 
 *                   Note that the dest_id is not filled in this function. When this update message 
 *                   is sent to all neighbors of the router, the dest_id is filled.
 */
void ConvertTabletoPkt(struct pkt_RT_UPDATE *UpdatePacketToSend, int myID){
	int i;
	UpdatePacketToSend->sender_id = myID;
	UpdatePacketToSend->no_routes = NumRoutes;
	
	for(i = 0; i < NumRoutes; i++){
		UpdatePacketToSend->route[i] = routingTable[i];
	}
}



/* Routine Name    : PrintRoutes
 * INPUT ARGUMENTS : 1. (FILE *) - Pointer to the log file created in router.c, with a filename that uses MyRouter's id.
 *                   2. int - My router's id received from command line argument.
 * RETURN VALUE    : void
 * USAGE           : This routine prints the routing table to the log file 
 *                   according to the format and rules specified in the Handout.
 */
void PrintRoutes (FILE* Logfile, int myID){
	fprintf(Logfile, "Routing Table:\n");
	int i = 0;
	int j = 0;
	while(i < MAX_ROUTERS){
		while(j < NumRoutes){
			if(routingTable[j].dest_id == i){
				fprintf(Logfile, "R%d -> R%d: R%d, %d\n", myID, i, routingTable[j].next_hop, routingTable[j].cost);
			}
			j++;
		}
		i++;
	}
	
}



/* Routine Name    : UninstallRoutesOnNbrDeath
 * INPUT ARGUMENTS : 1. int - The id of the inactive neighbor 
 *                   (one who didn't send Route Update for FAILURE_DETECTION seconds).
 *                   
 * RETURN VALUE    : void
 * USAGE           : This function is invoked when a nbr is found to be dead. The function checks all routes that
 *                   use this nbr as next hop, and changes the cost to INFINITY.
 */
void UninstallRoutesOnNbrDeath(int DeadNbr){
	int i = 0;
	while(i < NumRoutes){
		if(routingTable[i].next_hop == DeadNbr){
			routingTable[i].cost = INFINITY;
		}
		i++;
	}
}
