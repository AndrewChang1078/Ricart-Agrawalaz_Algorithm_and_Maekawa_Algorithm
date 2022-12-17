CC=g++

all: D

D: main.o server.o client.o
	$(CC) main.o server.o client.o -o D -pthread -std=c++11

main.o: main.cpp maekawa.h
	$(CC) -c main.cpp -pthread -std=c++11 

server.o: server.cpp maekawa.h
	$(CC) -c server.cpp -pthread -std=c++11

client.o: client.cpp maekawa.h
	$(CC) -c client.cpp -pthread -std=c++11


.PHONY: clean
clean:
	rm *.o
	rm D
	rm log/*
	rm s0/*
	rm s1/*
	rm s2/*
	echo "aaa" > s0/a.txt
	echo "aaa" > s1/a.txt
	echo "aaa" > s2/a.txt
	echo "bbb" > s0/b.txt
	echo "bbb" > s1/b.txt
	echo "bbb" > s2/b.txt
	echo "ccc" > s0/c.txt
	echo "ccc" > s1/c.txt
	echo "ccc" > s2/c.txt
