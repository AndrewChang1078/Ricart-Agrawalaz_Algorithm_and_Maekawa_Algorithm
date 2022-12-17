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
#include <sys/time.h>

#define SERVER_0_PORT 8080
#define SERVER_1_PORT 8081
#define SERVER_2_PORT 8082
#define SERVER_0_SERVER_PORT 8083
#define SERVER_1_SERVER_PORT 8084
#define SERVER_2_SERVER_PORT 8085
#define CLIENT_0_PORT 8086
#define CLIENT_1_PORT 8087
#define CLIENT_2_PORT 8088
#define CLIENT_3_PORT 8089
#define CLIENT_4_PORT 8090
#define QUOREM_MEMBER_COUNT 3

struct message {
	std::string file_name;
    int timestamp;
    std::string content;
};

struct mutual_exclusion_request {
	int server_id;
	int timestamp;
};

struct maekawa_control_info {
	std::unordered_map<std::string, int> local_ts;
	std::unordered_map<std::string, std::vector<std::vector<int>>> vReceivedRequests;
	std::unordered_map<std::string, bool> is_locked;
	std::unordered_map<std::string, bool> is_inquire_sent;
	std::unordered_map<std::string, int> cur_lock_proc;
	std::unordered_map<std::string, int> cur_lock_proc_ts;
	std::unordered_map<std::string, bool> is_received_failed;
	std::vector<int> quorem_list;
	std::unordered_map<std::string, std::vector<bool>> quorem_grant;
	std::unordered_map<std::string, bool> can_enter_cs;
	std::vector<int> client_socket;
	std::mutex mtx;
	std::unordered_map<std::string, int> highest_ts;
	std::unordered_map<std::string, std::vector<int>> unrespond_inquire_proc;
};

enum message_type {
	REQUEST,
	LOCKED,
	INQUIRE,
	FAILED,
	RELINQUISH,
	RELEASE
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
void send_message(message_type, int, int, std::string, std::ofstream&, bool, int, maekawa_control_info&);
