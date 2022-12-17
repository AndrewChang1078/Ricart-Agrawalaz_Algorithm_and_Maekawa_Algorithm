#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <thread>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <queue>
#include <sys/types.h>
#include <dirent.h>
#include <mutex>
#include <arpa/inet.h>
#include <unordered_map>
#include <algorithm>

#define SERVER_0_PORT 8080
#define SERVER_1_PORT 8081
#define SERVER_2_PORT 8082
#define SERVER_0_SERVER_PORT 8083
#define SERVER_1_SERVER_PORT 8084
#define SERVER_2_SERVER_PORT 8085

struct message {
	std::string file_name;
    int timestamp;
    std::string content;
};

struct mutual_exclusion_request{
	int server_id;
	int timestamp;
};

void run_server(int);
void run_client(int);
void client0_op(void);
void client1_op(void);
void client2_op(void);
void client3_op(void);
void client4_op(void);
void client_bye(void);
void client_enquiry(void);
void client_write(std::string, std::string);
int connect_socket(int&, std::string, int);
int create_sock(int&, int);
