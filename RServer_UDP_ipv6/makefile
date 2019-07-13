Rserver_udp.exe		: 	Rserver_udp.o 
	g++ -Wall -O2 Rserver_udp.cpp -lws2_32 -o Rserver_udp.exe 
			
Rserver_udp.o		:	Rserver_udp.cpp myrandomizer.h
	g++ -c -O2 -Wall Rserver_udp.cpp
	
clean:
	del *.o
	del *.exe