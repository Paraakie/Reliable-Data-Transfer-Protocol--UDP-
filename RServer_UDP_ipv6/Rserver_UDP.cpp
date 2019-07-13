//159.334 - Networks
//Sven Gerhards, 15031719
// SERVER: prototype for assignment 2.
//Note that this progam is not yet cross-platform-capable
// This code is different than the one used in previous semesters...
//************************************************************************/
//RUN WITH: Rserver_UDP 1235 0 0 
//RUN WITH: Rserver_UDP 1235 1 0 
//RUN WITH: Rserver_UDP 1235 0 1 
//RUN WITH: Rserver_UDP 1235 1 1  
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
#endif

#include "myrandomizer.h" 

using namespace std;

#define BUFFER_SIZE 80  //used by receive_buffer and send_buffer
//the BUFFER_SIZE needs to be at least big enough to receive the packet
#define SEGMENT_SIZE 78

const int ARG_COUNT = 4;
int numOfPacketsDamaged = 0;
int numOfPacketsLost = 0;
int numOfPacketsUncorrupted = 0;

int packets_damagedbit = 0;
int packets_lostbit = 0;

//*******************************************************************
//Function to save lines and discard the header
//*******************************************************************
//You are allowed to change this. You will need to alter the NUMBER_OF_WORDS_IN_THE_HEADER if you add a CRC
#define NUMBER_OF_WORDS_IN_THE_HEADER 2

void save_line_without_header(char * receive_buffer, FILE *fout) {
	//char *sep = " "; //separator is the space character

	char sep[3];

	strcpy(sep, " "); //separator is the space character
	char *word;
	int wcount = 0;
	char dataExtracted[BUFFER_SIZE] = "\0";

	// strtok remembers the position of the last token extracted.
	// strtok is first called using a buffer as an argument.
	// successive calls requires NULL as an argument.
	// the function remembers internally where it stopped last time

	//loop while word is not equal to NULL.
	for (word = strtok(receive_buffer, sep); word; word = strtok(NULL, sep)) {
		wcount++;
		if (wcount > NUMBER_OF_WORDS_IN_THE_HEADER) { //if the word extracted is not part of the header anymore
			strcat(dataExtracted, word); //extract the word and store it as part of the data
			strcat(dataExtracted, " "); //append space
		}
	}

	dataExtracted[strlen(dataExtracted) - 1] = (char) '\0'; //get rid of last space appended
	printf("DATA: %s, %d elements\n", dataExtracted,
			int(strlen(dataExtracted)));

	//make sure that the file pointer has been properly initialised
	if (fout != NULL)
		fprintf(fout, "%s\n", dataExtracted); //write to file
	else {
		printf("Error in writing to write...\n");
		exit(1);
	}
}

// CRCpolynomial
// used to create the CRC_NUM
#define GENERATOR 0x8005 //0x8005, generator for polynomial division

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

// Function used to get parts of recieved message to identify issues with file
void extractTokens(char *str, char *CRC, char *command, int &packetNumber, char *otherPNum,
		char *data) {
	char * pch;

	int tokenCounter = 0;
	//printf("Splitting string \"%s\" into tokens:\n\n", str);

	while (1) {
		if (tokenCounter == 0) {
			pch = strtok(str, " ,.-'\r\n'");
		} else {
			pch = strtok(NULL, " ,.-'\r\n'");
		}
		if (pch == NULL)
			break;
		/* For testing
		  printf("Token[%d], with %d characters = %s\n", tokenCounter,
		  		int(strlen(pch)), pch);
		 */
		switch (tokenCounter) {
		case 0:
			strcpy(CRC, pch); //copy CRC to the variable
			//printf("\tcode = %s\n", CRC);
			break;
		case 1: //command = new char[strlen(pch)];
			strcpy(command, pch);

			//printf("\tcommand = %s, %d characters\n", command, int(strlen(command)));
			break;
		case 2:
			packetNumber = atoi(pch);
			
			//printf("\tpacketNum = %d, %d characters\n", packetNumber, int(strlen(data)));
			break;
		case 3: // This is the word data 
			//data = new char[strlen(pch)];
			strcpy(data, pch);

			//printf("\tdata = %s, %d characters\n", data, int(strlen(data)));
			break;
		case 4:
			strcpy(otherPNum, pch);
			
			//printf("\tother packetNum = %s, %d characters\n", otherPNum, int(strlen(data)));
			break;
		case 5:
			strcpy(data, pch);
			
			//printf("\tdata = %s, %d characters\n", data, int(strlen(data)));
			break;		
		}

		tokenCounter++;
	}
}

#define WSVERS MAKEWORD(2,0)
WSADATA wsadata;

//*******************************************************************
//MAIN
//*******************************************************************
int main(int argc, char *argv[]) {
//********************************************************************
// INITIALIZATION
//********************************************************************
	struct sockaddr_storage clientAddress; //IPv4 & IPv6 -compliant
	struct addrinfo *result = NULL;
	struct addrinfo hints;
	int iResult;

	SOCKET s;
	char send_buffer[BUFFER_SIZE], receive_buffer[BUFFER_SIZE];
	int n, bytes, addrlen;

	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_INET; //AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE; // For wildcard IP address 

	randominit();
//********************************************************************
// WSSTARTUP
//********************************************************************
	if (WSAStartup(WSVERS, &wsadata) != 0) {
		WSACleanup();
		printf("WSAStartup failed\n");
	}

	if (argc != ARG_COUNT) {
		printf(
				"USAGE: Rserver_UDP localport allow_corrupted_bits(0 or 1) allow_packet_loss(0 or 1)\n");
		exit(1);
	}

	iResult = getaddrinfo(NULL, argv[1], &hints, &result); //converts human-readable text strings representing hostnames or IP addresses 
														   //into a dynamically allocated linked list of struct addrinfo structures
														   //IPV4 & IPV6-compliant

	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		WSACleanup();
		return 1;
	}

//********************************************************************
//SOCKET
//********************************************************************
	s = INVALID_SOCKET; //socket for listening
	// Create a SOCKET for the server to listen for client connections

	s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

	//check for errors in socket allocation
	if (s == INVALID_SOCKET) {
		printf("Error at socket(): %d\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		exit(1);	//return 1;
	}

	packets_damagedbit = atoi(argv[2]);
	packets_lostbit = atoi(argv[3]);
	if (packets_damagedbit < 0 || packets_damagedbit > 1 || packets_lostbit < 0
			|| packets_lostbit > 1) {
		printf(
				"USAGE: Rserver_UDP localport allow_corrupted_bits(0 or 1) allow_packet_loss(0 or 1)\n");
		exit(0);
	}
	
	// Counter = number switches between 0 and 1
	int counter = 0;
	int lastStoredData = -1; // keep track of last data stored, to avoid duplicate storage
//********************************************************************
//BIND
//********************************************************************
	iResult = bind(s, result->ai_addr, (int) result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);

		closesocket(s);
		WSACleanup();
		return 1;
	}

	cout << "==============<< UDP SERVER >>=============" << endl;
	cout << "159334 Sven Gerhards, 15031719" << endl;
	cout << "channel can damage packets=" << packets_damagedbit << endl;
	cout << "channel can lose packets=" << packets_lostbit << endl;

	freeaddrinfo(result); //free the memory allocated by the getaddrinfo 
								//function for the server's address, as it is 
								//no longer needed
//********************************************************************
// Open file to save the incoming packets
//********************************************************************
//In text mode, carriage return–linefeed combinations 
//are translated into single linefeeds on input, and 
//linefeed characters are translated to carriage return–linefeed combinations on output.    
	FILE *fout = fopen("data_received.txt", "w");


//********************************************************************
// Variables for Extraction
//********************************************************************
	
	//Extract CRC_NUM & packetNumber & data
	int packetNumber;
	char orignal_CRC_char[10]; //extracted CRC
	unsigned int orignal_CRC = 0;
	unsigned int new_CRC = 0; // generated-with-extracted-data CRC
	
	char pNum_char[256];
	char otherPNum[256];
	
	char data[256];
	char command[256];
	char temp_buffer[BUFFER_SIZE] = ""; //use this to recreate a packet contents
	
	//Time values
	clock_t startTime, currentTime;

//********************************************************************
//INFINITE LOOP
//********************************************************************
	while (1) {
//********************************************************************
//RECEIVE
//********************************************************************
//printf("Waiting... \n");		
		addrlen = sizeof(clientAddress); //IPv4 & IPv6-compliant
		memset(receive_buffer, 0, sizeof(receive_buffer));
		bytes = recvfrom(s, receive_buffer, SEGMENT_SIZE, 0,
				(struct sockaddr*) &clientAddress, &addrlen);

//********************************************************************
//IDENTIFY UDP client's IP address and port number.     
//********************************************************************      
		char clientHost[NI_MAXHOST];
		char clientService[NI_MAXSERV];
		memset(clientHost, 0, sizeof(clientHost));
		memset(clientService, 0, sizeof(clientService));

		getnameinfo((struct sockaddr *) &clientAddress, addrlen, clientHost,
				sizeof(clientHost), clientService, sizeof(clientService),
				NI_NUMERICHOST);

		printf("\nReceived a packet of size %d bytes from <<<UDP Client>>> with IP address:%s, at Port:%s\n",
				bytes, clientHost, clientService);

//********************************************************************
//PROCESS RECEIVED PACKET
//********************************************************************
		printf("\n================================================\n");
		printf("RECEIVED --> %s \n", receive_buffer);

		
		//Remove data from previous package
		strcpy(temp_buffer, "");
		strcpy(pNum_char, "");
		strcpy(otherPNum, "");
		strcpy(data, "");
		new_CRC = 0;
		orignal_CRC = -1;
		
		// Only run this code, if no CLOSE message was sent
		if (strncmp(receive_buffer, "CLOSE", 5) != 0 && strcmp(receive_buffer, "\n") != 0) {
			//Extract data from receive_buffer
			extractTokens(receive_buffer, orignal_CRC_char, command, packetNumber, otherPNum, data);
			
			//convert from char[] to unsigned int
			orignal_CRC =  atoi(orignal_CRC_char);
			
			//Use exctracted data to generate new_CRC
			sprintf(pNum_char, "%d ", packetNumber); //recreate packet header with Sequence number
			
			strcpy(temp_buffer, "data ");
			strcat(temp_buffer, otherPNum);
			strcat(temp_buffer, " ");
			strcat(temp_buffer, data);			
			strcat(temp_buffer, "\r\n"); //add "\r\n"
			
			//restore the orignal without CRC
			sprintf(receive_buffer, "PACKET %d ", packetNumber);
			strcat(receive_buffer, temp_buffer);
			
			//for testing |->|printf("\nreceive_buffer = (%s)\n", receive_buffer);
			
			//sprintf(new_CRC, "%u", CRCpolynomial(temp_buffer));
			new_CRC = CRCpolynomial(receive_buffer);
		}
		
		//Remove trailing CR ('\r') and LF('\n')
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

		//Check if sent data was only '\n'
		if ((bytes < 0) || (bytes == 0))
			break;
		
		/* 
		 * Compare orignal_CRC with new_CRC
		 * if correct continue
		 */
		if(orignal_CRC == new_CRC){
			
			//printf("\nCRC are matching\n");

			//Updates counter based on arrived PACKET
			sscanf(receive_buffer, "PACKET %d", &counter);
			
//********************************************************************
//SEND ACK
//********************************************************************
			sprintf(send_buffer, "ACK %d \r\n", counter);

			//send ACK unreliably
			send_unreliably(s, send_buffer, (sockaddr*) &clientAddress);

			/*************************************************************
			 * store the packet's data into a file
			 *************************************************************/
			if(lastStoredData != counter){
				save_line_without_header(receive_buffer, fout);
				lastStoredData = counter;
			}
		} else { 
			// Run when CLOSE message was received 
			if (strncmp(receive_buffer, "CLOSE", 5) == 0) { //if client says "CLOSE", the last packet for the file was sent. Close the file
				
				//send ACK
				sprintf(send_buffer, "ACK FIN\r\n"); 
				send_unreliably(s, send_buffer, (sockaddr*) &clientAddress);
				
				// send CLOSE
				sprintf(send_buffer, "CLOSE\r\n"); 
				send_unreliably(s, send_buffer, (sockaddr*) &clientAddress);
				
				
				//Infinite Loop
				startTime = clock();
				while(true){
						
					/*
					 * Wait for "ACK FIN" 
					 * (will be sent in response to CLOSE)
					 */
					addrlen = sizeof(clientAddress); //IPv4 & IPv6-compliant
					bytes = recvfrom(s, receive_buffer, 78, 0,
					(struct sockaddr*) &clientAddress, &addrlen);
					
					// break, once ACK has been received
					if(bytes != 0 || !(bytes < 0)){
						
						//if ACK FIN was received
						if(strncmp(receive_buffer, "ACK FIN", 7) == 0){
							printf("\nACK FIN has arrived\n");
							break;
						} else {
							sprintf(send_buffer, "NAK\r\n"); 
							send_unreliably(s, send_buffer, (sockaddr*) &clientAddress);
						}
					}
					
					//send an ACK and CLOSE
					currentTime = clock();
					//if time is up send again
					if((double)(currentTime - startTime) / CLOCKS_PER_SEC >= 1){
						//send ACK
						sprintf(send_buffer, "ACK FIN\r\n"); 
						send_unreliably(s, send_buffer, (sockaddr*) &clientAddress);
						
						// send CLOSE
						sprintf(send_buffer, "CLOSE\r\n"); 
						send_unreliably(s, send_buffer, (sockaddr*) &clientAddress);
						startTime = clock();
					}
				}	
				fclose(fout);
				closesocket(s);
				
				printf("Server saved data_received.txt \n");//you have to manually check to see if this file is identical to file1_Windows.txt
				printf("Closing the socket connection and Exiting...\n");
				break;
			} 
			
			//File is corrupt or damaged
			else { 
				printf("\nCRC Numbers are not matching, seding NAK %d\n", counter);
				//for testing |->| printf("orignal_CRC = %u\nnew_CRC = %u\n", orignal_CRC, new_CRC);
			
				// Send Negative ACK
				sprintf(send_buffer, "NAK %d \r\n", counter);
				send_unreliably(s, send_buffer, (sockaddr*) &clientAddress);
			}
		}
	}
	closesocket(s);

	cout << "==============<< STATISTICS >>=============" << endl;
	cout << "numOfPacketsDamaged=" << numOfPacketsDamaged << endl;
	cout << "numOfPacketsLost=" << numOfPacketsLost << endl;
	cout << "numOfPacketsUncorrupted=" << numOfPacketsUncorrupted << endl;
	cout << "===========================================" << endl;

	exit(0);
}
