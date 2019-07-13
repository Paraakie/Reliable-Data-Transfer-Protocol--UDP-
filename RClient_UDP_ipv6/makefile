Rclient_udp.exe		: 	Rclient_udp.o 
	g++ -Wall -O2 Rclient_udp.cpp -lws2_32 -o Rclient_udp.exe 
			
Rclient_udp.o		:	Rclient_udp.cpp myrandomizer.h
	g++ -c -O2 -Wall Rclient_udp.cpp
	
clean:
	del *.o
	del *.exe