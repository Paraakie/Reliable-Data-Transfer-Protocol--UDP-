
//159.334 - Networks
// This code is different than the one used in previous semesters...
//*******************************************************************************************************************************//
//you are NOT allowed to change these functions
//*******************************************************************************************************************************//
//myrandomizer_windows

#ifndef __MYRANDOMIZER_H__
#define __MYRANDOMIZER_H__

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

#elif defined _WIN32 

	//Ws2_32.lib
	#define _WIN32_WINNT 0x501  //to recognise getaddrinfo()

	//"For historical reasons, the Windows.h header defaults to including the Winsock.h header file for Windows Sockets 1.1. The declarations in the Winsock.h header file will conflict with the declarations in the Winsock2.h header file required by Windows Sockets 2.0. The WIN32_LEAN_AND_MEAN macro prevents the Winsock.h from being included by the Windows.h header"
	#ifndef WIN32_LEAN_AND_MEAN
	   #define WIN32_LEAN_AND_MEAN
	#endif


	#include <stdio.h>
	#include <time.h>
	#include <stdlib.h>
	#include <assert.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
#endif

//********************************************
// fate=0 (ok)  or 
//     =1 (damaged) or 
//     =2 (lost)
//********************************************
//---                 
int damaged_not_lost_cases[10] = {1,0,1,0,0,1,1,0,0,0};                   
int lost_not_damaged_cases[10] = {0,2,0,0,0,0,2,2,0,2};                   
int damaged_and_lost_cases[10] = {1,0,2,0,0,0,2,1,1,0};                     
//---

extern int numOfPacketsDamaged;
extern int numOfPacketsLost;
extern int numOfPacketsUncorrupted;
//---
extern int packets_damagedbit;
extern int packets_lostbit;

void randominit(void){
	// srand(cputime());
	srand((unsigned)time(NULL));
}

float randomVal(float min, float max)
{
	float r;
	r = (float)rand()/RAND_MAX;
	r = min + (r*(max-min));
	return r;
}


int packets_fate(void){
	static int counter=0;
	int tmp;	
		
	if (packets_damagedbit==0 && packets_lostbit==0) return 0;
	if (packets_damagedbit==1 && packets_lostbit==0) {
		tmp = counter;
		counter++;
		counter = counter % 10;
		return damaged_not_lost_cases[tmp];
		
	}
	if (packets_damagedbit==0 && packets_lostbit==1) {
		tmp = counter;
		counter++;
		counter = counter % 10;
		return lost_not_damaged_cases[tmp];
	}
	if (packets_damagedbit==1 && packets_lostbit==1) {
		tmp = counter;
		counter++;
		counter = counter % 10;
		return damaged_and_lost_cases[tmp];
	} else {
		printf("Error: unknown case in packets_fate().\n");
		exit(1);
	}		
}

int damage_bit(void){
	return (int) (10.0 * (rand() / (RAND_MAX + 1.0)));
}

int random_char(void){
	return (int) (255.0 * (rand() / (RAND_MAX + 1.0)));
}



int send_unreliably(int s,const char *send_buffer, struct sockaddr* remoteaddress) {
	    char tmp_send_buffer[256];
	
	    //if(send_buffer == NULL) return 0;
	    strcpy(tmp_send_buffer,send_buffer);
	   
	    int bytes=0;
   	    int fate=packets_fate(); //determine the packet's fate
	    
		if (fate==0){//fate=0 (ok)  or 1 (damaged) or 2 (lost)
            numOfPacketsUncorrupted++; 
			bytes = sendto(s, tmp_send_buffer, strlen(tmp_send_buffer),0,(struct sockaddr *)(remoteaddress),sizeof(sockaddr_storage) );	
			printf("<-- SEND: %s,%d elements\n",tmp_send_buffer,int(strlen(tmp_send_buffer)));
			if (bytes < 0) {
				printf("send failed\n");
				exit(1);
			}
		} else if (fate== 1){
			    numOfPacketsDamaged++;
				printf("TRIED %s, size = %d\n",tmp_send_buffer, int(strlen(tmp_send_buffer)));
				tmp_send_buffer[damage_bit()]=random_char();
				tmp_send_buffer[damage_bit()]=random_char();
				
			    bytes = sendto(s, tmp_send_buffer, strlen(tmp_send_buffer),0,(struct sockaddr *)(remoteaddress),sizeof(sockaddr_storage) );
			   
				printf("<-- DAMAGED %s \n",tmp_send_buffer);
				if (bytes < 0) {
		  	       printf("send failed\n");
	     	 		 exit(1);
				}
		} else if(fate==2){
			    numOfPacketsLost++;
				printf("X-- LOST %s \n",tmp_send_buffer);
				//do nothing, lose the packet
		} else {
				printf("Error in send_unreliably: unknown packet fate.\n");
				exit(1);
		}
		
		return bytes;
}

///////////////////////////////////////////////////////////////////////////////
#endif
