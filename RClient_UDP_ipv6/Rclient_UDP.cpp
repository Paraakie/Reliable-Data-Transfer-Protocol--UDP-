//159.334 - Networks
//Sven Gerhards, 15031719
// CLIENT: prototype for assignment 2. 
//Note that this progam is not yet cross-platform-capable
// This code is different than the one used in previous semesters...
//************************************************************************/
//RUN WITH: Rclient_UDP 127.0.0.1 1235 0 0 WORKS!
//RUN WITH: Rclient_UDP 127.0.0.1 1235 0 1 
//RUN WITH: Rclient_UDP 127.0.0.1 1235 1 0 
//RUN WITH: Rclient_UDP 127.0.0.1 1235 1 1 
//************************************************************************/

//---
#if defined __unix__ || defined __APPLE__
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h> 
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <netinet/in.h> 
#include <netdb.h>
#include <iostream>

#elif defined _WIN32 

//Ws2_32.lib
#define _WIN32_WINNT 0x501  //to recognise getaddrinfo()

//"For historical reasons, the Windows.h header defaults to including the Winsock.h header file for Windows Sockets 1.1. The declarations in the Winsock.h header file will conflict with the declarations in the Winsock2.h header file required by Windows Sockets 2.0. The WIN32_LEAN_AND_MEAN macro prevents the Winsock.h from being included by the Windows.h header"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <time.h> // For use of Clock()
#endif

#include "myrandomizer.h"

using namespace std;

#define WSVERS MAKEWORD(2,0)
#define BUFFER_SIZE 80  //used by receive_buffer and send_buffer
//the BUFFER_SIZE has to be at least big enough to receive the packet
#define SEGMENT_SIZE 78
//segment size, i.e., if fgets gets more than this number of bytes it segments the message into smaller parts.

WSADATA wsadata;
const int ARG_COUNT = 5;
//---
int numOfPacketsDamaged = 0;
int numOfPacketsLost = 0;
int numOfPacketsUncorrupted = 0;

int packets_damagedbit = 0;
int packets_lostbit = 0;

#define GENERATOR 0x8005 //0x8005, generator for polynomial division

// CRCpolynomial
// used to recreate the CRC_NUM, if it doesn't match
unsigned int CRCpolynomial(char *buffer) {
	unsigned char i;
	unsigned int rem = 0x0000;
	unsigned int bufsize = strlen(buffer);
	while (bufsize-- != 0) {
		for (i = 0x80; i != 0; i /= 2) {
			if ((rem & 0x8000) != 0) {
				rem = rem << 1;
				rem ^= GENERATOR;
			} else {
				rem = rem << 1;
			}
			if ((*buffer & i) != 0) {
				rem ^= GENERATOR;
			}
		}
		buffer++;
	}
	rem = rem & 0xffff;
	return rem;
}

//*******************************************************************
//MAIN
//*******************************************************************
int main(int argc, char *argv[]) {
//*******************************************************************
// Initialization
//*******************************************************************
	struct sockaddr_storage localaddr, remoteaddr;
	char portNum[NI_MAXSERV];
	struct addrinfo *result = NULL;
	struct addrinfo hints;

	memset(&localaddr, 0, sizeof(localaddr));  //clean up
	memset(&remoteaddr, 0, sizeof(remoteaddr));  //clean up  
	randominit();
	SOCKET s;
	char send_buffer[BUFFER_SIZE], receive_buffer[BUFFER_SIZE];
	int n, bytes, addrlen;

	addrlen = sizeof(struct sockaddr);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; //AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	// Used for timer
	clock_t startTime, currentTime;
	clock_t startTimeout; //This value will be exclusively used for the timeout
	
	//Used to check fi both ACKFIN and CLOSE have been received
	bool ACKFIN_received = false;
	bool CLOSE_received = false;
	
//********************************************************************
// WSSTARTUP
//********************************************************************
	if (WSAStartup(WSVERS, &wsadata) != 0) {
		WSACleanup();
		printf("WSAStartup failed\n");
	}
//*******************************************************************
//	Dealing with user's arguments
//*******************************************************************
	if (argc != ARG_COUNT) {
		printf(
				"USAGE: Rclient_UDP remote_IP-address remoteport allow_corrupted_bits(0 or 1) allow_packet_loss(0 or 1)\n");
		exit(1);
	}

	int iResult = 0;

	sprintf(portNum, "%s", argv[2]);
	iResult = getaddrinfo(argv[1], portNum, &hints, &result);

	packets_damagedbit = atoi(argv[3]);
	packets_lostbit = atoi(argv[4]);
	if (packets_damagedbit < 0 || packets_damagedbit > 1 || packets_lostbit < 0
			|| packets_lostbit > 1) {
		printf(
				"USAGE: Rclient_UDP remote_IP-address remoteport allow_corrupted_bits(0 or 1) allow_packet_loss(0 or 1)\n");
		exit(0);
	}

//*******************************************************************
//CREATE CLIENT'S SOCKET 
//*******************************************************************
	s = INVALID_SOCKET;
	s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

	if (s == INVALID_SOCKET) {
		printf("socket failed\n");
		exit(1);
	}
	//nonblocking option
	// Set the socket I/O mode: In this case FIONBIO
	// enables or disables the blocking mode for the 
	// socket based on the numerical value of iMode.
	// If iMode = 0, blocking is enabled; 
	// If iMode != 0, non-blocking mode is enabled.
	u_long iMode = 1;

	iResult = ioctlsocket(s, FIONBIO, &iMode);
	if (iResult != NO_ERROR) {
		printf("ioctlsocket failed with error: %d\n", iResult);
		closesocket(s);
		WSACleanup();
		exit(0);
	}

	cout << "==============<< UDP CLIENT >>=============" << endl;
	cout << "159334 Sven Gerhards, 15031719" << endl;
	
	cout << "channel can damage packets=" << packets_damagedbit << endl;
	cout << "channel can lose packets=" << packets_lostbit << endl;

//*******************************************************************
//SEND A TEXT FILE 
//*******************************************************************
	int counter = 0;
	char temp_buffer[BUFFER_SIZE];

	//using 10 like tokenizer file does
	char CRC_NUM[10] = ""; //store the crc_num

	FILE *fin = fopen("data_for_transmission.txt", "rb"); //original

//In text mode, carriage return–linefeed combinations 
//are translated into single linefeeds on input, and 
//linefeed characters are translated to carriage return–linefeed combinations on output. 

	if (fin == NULL) {
		printf("cannot open data_for_transmission.txt\n");
		closesocket(s);
		WSACleanup();
		exit(0);
	} else {
		printf("data_for_transmission.txt is now open for sending\n");
	}
	
	while (1) {
		memset(send_buffer, 0, sizeof(send_buffer)); //clean up the send_buffer before reading the next line
		if (!feof(fin)) {
			fgets(send_buffer, SEGMENT_SIZE, fin); //get one line of data from the file

			//for testing |->|printf("\nsend_buffer = (%s)\n", send_buffer);
			
			//Skip Empty Lines
			if(strcmp(send_buffer, "") == 0 ||
				strcmp(send_buffer, "\n") == 0 ||
				strcmp(send_buffer, "\0") == 0){
				
				continue;
			}
			
			sprintf(temp_buffer, "PACKET %d ", counter); //create packet header with Sequence number
			
			//Update Counter
			if(counter == 0){
				counter = 1;
			} else if(counter == 1){
				counter = 0;
			} else {
				counter = 0;
				//for testing |->| 
				printf("\ncounter has invalid value\n");
			}
			
			strcat(temp_buffer, send_buffer);   //append data to packet header

			// use CRCpolynomial to get a unique key, convert to char[]
			sprintf(CRC_NUM, "%u", CRCpolynomial(temp_buffer));
			
			// Add CRC_NUM at the front of temp_buffer
			strcat(CRC_NUM, " ");
			strcat(CRC_NUM, temp_buffer);
			strcpy(temp_buffer, CRC_NUM);
			
			strcat(temp_buffer, "\r\n"); //add "\r\n"
			strcpy(send_buffer, temp_buffer);   //the complete packet

			printf("\n======================================================\n");
			cout << "calling send_unreliably, to deliver data of size "
					<< strlen(send_buffer) << endl;

			/*
			 * Actually sends the data
			 */
			send_unreliably(s, send_buffer, (result->ai_addr)); //send the packet to the unreliable data channel

			// Start/Reset Timer
			startTime = clock();
			// times Package a package is resent
			
			int count_PACK_send = 0; 
			
			//Wait until receiving an ACK
			while(true){
					
					Sleep(1); //sleep for 1 millisecond																			
//********************************************************************
//************ RECEIVE
//********************************************************************
					addrlen = sizeof(remoteaddr); //IPv4 & IPv6-compliant
					bytes = recvfrom(s, receive_buffer, 78, 0,
					(struct sockaddr*) &remoteaddr, &addrlen);
					
					// break, once ACK has been received
					if(bytes != 0 || !(bytes < 0)){
						
						//ACK 0
						if(strncmp(receive_buffer, "ACK 0", 5) == 0 && counter == 0){
							printf("\nACK 0 has arrived\n");
							break;
						} 
						//ACK 1
						else if(strncmp(receive_buffer, "ACK 1", 5) == 0 && counter == 1){
							printf("\nACK 1 has arrived\n");
							break;
						} 
						//NAK 0
						else if(strncmp(receive_buffer, "NAK 0", 5) == 0){
							printf("\nNAK 0 has arrived, sending new packet\n");
							send_unreliably(s, send_buffer, (result->ai_addr)); //send the packet to the unreliable data channel
							//reset timer
							startTime = clock();
							count_PACK_send++;
						} 
						//NACK 1
						else if(strncmp(receive_buffer, "NAK 1", 5) == 0){
							printf("\nNAK 1 has arrived, sending new packet\n");
							send_unreliably(s, send_buffer, (result->ai_addr)); //send the packet to the unreliable data channel
							//reset timer
							startTime = clock();
							count_PACK_send++;
						} 
						//Corrupted File
						else {
							//do nothing
							//for testing |->|printf("\n%s has arrived", receive_buffer);
						}
					}
					
					currentTime = clock(); //get Current Time
					
					//if time is up send again
					if((double)(currentTime - startTime) / CLOCKS_PER_SEC >= 1){
						
						send_unreliably(s, send_buffer, (result->ai_addr)); //send the packet to the unreliable data channel
						
						//reset timer
						startTime = clock();
						count_PACK_send++;
						
					}
					//temporary solution to avoid infinite loops
					if(count_PACK_send >= 10){
						//for testing |->|
						//printf("100 unsuccessful sent buffers - abort loop\n");
						break; 
					}
			}
//********************************************************************
//IDENTIFY server's IP address and port number.     
//********************************************************************      
			char serverHost[NI_MAXHOST];
			char serverService[NI_MAXSERV];
			memset(serverHost, 0, sizeof(serverHost));
			memset(serverService, 0, sizeof(serverService));

			getnameinfo((struct sockaddr *) &remoteaddr, addrlen, serverHost,
					sizeof(serverHost), serverService, sizeof(serverService),
					NI_NUMERICHOST);

			printf("\nReceived a packet of size %d bytes from <<<UDP Server>>> with IP address:%s, at Port:%s\n",
					bytes, serverHost, serverService);

//********************************************************************
//PROCESS REQUEST
//********************************************************************
			//Remove trailing CR and LN
			if (bytes != SOCKET_ERROR) {
				n = 0;
				while (n < bytes) {
					n++;
					if ((bytes < 0) || (bytes == 0))
						break;
					if (receive_buffer[n] == '\n') { /*end on a LF*/
						receive_buffer[n] = '\0';
						break;
					}
					if (receive_buffer[n] == '\r') /*ignore CRs*/
						receive_buffer[n] = '\0';
				}
				printf("RECEIVED --> %s, %d elements\n", receive_buffer,
						int(strlen(receive_buffer)));
			}
		// Start Closing the Connection
		} else {
			printf("End-of-File reached. \n");
			memset(send_buffer, 0, sizeof(send_buffer));
			
			/*
			 * Loop so that CLOSE will be resent if lost
			 */
			//send first CLOSE message
			sprintf(send_buffer, "CLOSE \r\n"); //send a CLOSE command to the RECEIVER (Server)
			send_unreliably(s, send_buffer, (result->ai_addr));
			
			// start timer			
			startTime = clock();
			//reset booleans
			ACKFIN_received = false;
			CLOSE_received = false;
			
			// Wait for ACK FIN & CLOSE
			while(true){
				
				addrlen = sizeof(remoteaddr); //IPv4 & IPv6-compliant
				bytes = recvfrom(s, receive_buffer, 78, 0,
				(struct sockaddr*) &remoteaddr, &addrlen);
				
				// break, once ACK has been received
				if(bytes != 0 || !(bytes < 0)){
					
					// Wait for ACK FIN
					if(strncmp(receive_buffer, "ACK FIN", 7) == 0 && !(ACKFIN_received)){
						printf("\nACK FIN has arrived\n");
						ACKFIN_received = true;
					} 
					
					// Wait for CLOSE
					if(strncmp(receive_buffer, "CLOSE", 5) == 0 && !(CLOSE_received)){
						printf("\nACK FIN has arrived\n");
						CLOSE_received = true;
					} 
					
					// If both commands have been received send ACK FIN
					if(ACKFIN_received && CLOSE_received){
						//Once receiving the CLOSE from RServer send ACK
						sprintf(send_buffer, "ACK FIN\r\n");
						send_unreliably(s, send_buffer, (result->ai_addr));
						
						break;
					}
					
					//When time is up send CLOSE again
					currentTime = clock();
					if((double)(currentTime - startTime) / CLOCKS_PER_SEC >= 1){
						send_unreliably(s, send_buffer, (result->ai_addr));
						startTime = clock();
					}
				}
			}
			
			
			//Timeout for 30 seconds
			startTimeout = clock();
			printf("Starting Timeout of 30 seconds\n");
			
			// Start Timer for ACK FIN
			startTime = clock();
			while(true){
				
				// Wait for NAK
				addrlen = sizeof(remoteaddr); //IPv4 & IPv6-compliant
				bytes = recvfrom(s, receive_buffer, 78, 0,
				(struct sockaddr*) &remoteaddr, &addrlen);
				
				//If data is received
				if(bytes != 0 || !(bytes < 0)){
					// If NAK is received
					if(strncmp(receive_buffer, "NAK", 3) == 0){
						// Send another ACK FIN
						send_unreliably(s, send_buffer, (result->ai_addr));
					}
				}
				
				currentTime = clock();
				if((double)(currentTime - startTime) / CLOCKS_PER_SEC >= 3){
					send_unreliably(s, send_buffer, (result->ai_addr));
					//reset timer
					startTime = clock();
				}
				
				currentTime = clock();
				if((double)(currentTime - startTimeout) / CLOCKS_PER_SEC >= 30){
					break;
				}
			}
			fclose(fin);
			printf("\n======================================================\n");
			break;
		}

	} //while loop
//*******************************************************************
//CLOSESOCKET   
//*******************************************************************
	closesocket(s);
	printf("Closing the socket connection and Exiting...\n");
	cout << "==============<< STATISTICS >>=============" << endl;
	cout << "numOfPacketsDamaged=" << numOfPacketsDamaged << endl;
	cout << "numOfPacketsLost=" << numOfPacketsLost << endl;
	cout << "numOfPacketsUncorrupted=" << numOfPacketsUncorrupted << endl;
	cout << "===========================================" << endl;

	exit(0);
}

