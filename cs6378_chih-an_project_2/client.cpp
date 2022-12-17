#include "ricart.h"

static thread_local int sfd = -1;
static thread_local std::ofstream flog;
static thread_local int client_id;
static thread_local std::vector<int> server_port={SERVER_0_PORT, SERVER_1_PORT, SERVER_2_PORT};
static thread_local std::string send_buf;
static thread_local char recv_buf[500];
static thread_local std::vector<std::string> file_list;
static thread_local std::unordered_map<std::string, int> local_ts;
static thread_local std::unordered_map<std::string, int> highest_ts;

void run_client(int id) {
	client_id=id;
	flog.open("log/c"+std::to_string(id)+".log");
	struct sockaddr_in serv_addr;
	int con_server_id;

	//Create a socket for communication
    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        flog<<"socket failed"<<std::endl;
        exit(EXIT_FAILURE);
    }
    flog<<"Socket created successfully"<<std::endl;

	// Connect to a random server0~2
    serv_addr.sin_family = AF_INET;
	con_server_id=rand()%3;//Connect to an arbitrary server
    serv_addr.sin_port = htons(server_port[con_server_id]);
	if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0) {
	    flog << "Invalid address/ Address not supported" << std::endl;
        exit(EXIT_FAILURE);
    }

	//Wait for server startup
	sleep(2);

	flog<<"Trying to connect to server.."<<con_server_id<<std::endl;

	//Connect the socket
    if (connect(sfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        flog << "connect failed" << std::endl;
        exit(EXIT_FAILURE);
    }
    flog << "Connected to the server" << std::endl;

	//Different operations for each server
	switch(client_id) {
		case 0:
			client0_op();
			break;
		case 1:
			client1_op();
			break;
		case 2:
			client2_op();
			break;
		case 3:
			client3_op();
			break;
		case 4:
			client4_op();
			break;
	}
	
    /* Close the socket */
    if (close(sfd) != 0) {
        perror("close failed");
		exit(EXIT_FAILURE);
    }
    flog << "Connection closed" << std::endl;
}

void client0_op() {
	client_enquiry();
	client_write("a.txt", "client0");
	client_write("b.txt", "client0");
	client_write("a.txt", "client0_2");
	client_write("b.txt", "client0_2");
	client_bye();
}

void client1_op() {
	client_enquiry();
	client_write("c.txt", "client1");
	client_write("b.txt", "client1");
	client_write("c.txt", "client1_2");
	client_write("a.txt", "client1");
	client_write("c.txt", "client1_3");
	client_bye();
}

void client2_op() {
	client_enquiry();
	client_write("a.txt", "client2");
	client_write("c.txt", "client2");
	client_write("a.txt", "client2_2");
	client_write("b.txt", "client2");
	client_bye();
}

void client3_op() {
	client_enquiry();
	client_write("c.txt", "client3");
	client_write("b.txt", "client3");
	client_write("b.txt", "client3_2");
	client_write("a.txt", "client3");
	client_bye();
}

void client4_op() {
	client_enquiry();
	client_write("c.txt", "client4");
	client_write("b.txt", "client4");
	client_write("a.txt", "client4");
	client_write("a.txt", "client4_2");
	client_bye();
}

void client_enquiry() {
	file_list.clear();
	send_buf="ENQUIRY";
	if (send(sfd, send_buf.c_str(), sizeof(send_buf.c_str()), 0) == -1) {
	    flog<<"send failed"<<std::endl;
	    exit(EXIT_FAILURE);
	}
	flog << "Send ENQUIRY" << std::endl;

	//Start receiving messages
	while (1) {
        if (read(sfd, recv_buf, sizeof(recv_buf)) == -1) {
	    	flog<<"read failed"<<std::endl;
	    	exit(EXIT_FAILURE);
		}
		flog<<"Receive "<<recv_buf<<std::endl;	
		char *tmp=NULL;
		tmp=strtok(recv_buf,",");
		while(tmp) {
			file_list.push_back(tmp);
			tmp=strtok(NULL,",");
		}
		//Clear the receive buffer
		bzero(recv_buf,sizeof(recv_buf));
		break;
	}

	//Logging the file list
	flog<<std::endl<<"Server hosted file list:"<<std::endl;
	for(auto f : file_list) {
		flog<<f<<std::endl;
		local_ts[f]=0;
		highest_ts[f]=0;
    }
	flog<<std::endl;
}

void client_bye() {
	send_buf="BYE";
	if (send(sfd, send_buf.c_str(), sizeof(send_buf.c_str()), 0) == -1) {
		flog<<"send failed"<<std::endl;
		exit(EXIT_FAILURE);
	}
	flog<<"Send "+send_buf<<std::endl;
}

void client_write(std::string file_name,std::string c) {
	//Send request
	local_ts[file_name]=std::max(highest_ts[file_name], local_ts[file_name])+1;
	send_buf="WRITE "+std::to_string(local_ts[file_name])+" "+file_name+" "+c;
	if (send(sfd, send_buf.c_str(), send_buf.size(), 0) == -1) {
		flog<<"send failed"<<std::endl;
		exit(EXIT_FAILURE);
	}
	flog<<"Send "+send_buf<<std::endl;
	send_buf.clear();

	//Waiting for response
	while (1) {
        if (read(sfd, recv_buf, sizeof(recv_buf)) == -1) {
	    	flog<<"read failed"<<std::endl;
	    	exit(EXIT_FAILURE);
		}
		flog<<"Receive "<<recv_buf<<std::endl;
		char *command=strtok(recv_buf, " ");
		if(strcmp(command,"OK")==0) {
			//Write successfully
			char *p;
			p=strtok(NULL, " ");
			highest_ts[file_name]=std::max(highest_ts[file_name], atoi(p));
			break;
		}else {
			flog<<"Client write failed"<<std::endl;
			exit(EXIT_FAILURE);
		}

		//Clear the receive buffer
		bzero(recv_buf,sizeof(recv_buf));
		break;
	}
}
