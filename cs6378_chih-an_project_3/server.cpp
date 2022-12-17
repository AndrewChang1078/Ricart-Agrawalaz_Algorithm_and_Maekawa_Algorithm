#include "maekawa.h"

static thread_local std::ofstream flog;//For logging
static thread_local std::string flog_path;
static thread_local std::vector<std::string> file_list;//Server hosted files

//Add server hosted files to the file_list
void set_file_list(int server_id) {
	DIR *dir;
    struct dirent *ent;
    const char *dir_path=("s"+std::to_string(server_id)+"/").c_str();

    if((dir=opendir(dir_path))!=NULL) {
        while((ent=readdir(dir))!=NULL) {
			std::string t=ent->d_name;
			if(t.size()>4 && strcmp(t.substr(t.size()-4, 4).c_str(),".txt")==0) {
	        	file_list.push_back(t);
			}
        }
	} else {
		flog<<"Open directory failed"<<std::endl;
		exit(EXIT_FAILURE);
	}
}

void process_message(int fd, int server_id, std::string message, std::ofstream &flog, std::unordered_map<std::string, std::ofstream> &server_files) {
	char *command = NULL,*p0=NULL, *p1 = NULL, *p2 = NULL;
	std::string send_buf;
	char *token;

	flog<<"Receive "<<message<<std::endl;
	token=(char*)message.c_str();
    command = strtok_r(token, " ", &token);

	//Process the received command
    if(strcmp(command, "REQ")==0) {
		//Reply OK
		send_buf="OK";
        if (send(fd, send_buf.c_str(), send_buf.size(), 0) == -1) {
 	       flog << "Sending response failed" << std::endl;
           exit(EXIT_FAILURE);
        }
		flog<<"Sent OK"<<std::endl;
		send_buf.clear();
    } else if(strcmp(command, "ENQUIRY")==0) {
        DIR *dir;
        struct dirent *ent;
        const char *dir_path=("s"+std::to_string(server_id)+"/").c_str();

		for(auto f : file_list) {
			send_buf=send_buf+f+",";
		}
		send_buf=send_buf.substr(0,send_buf.size()-1);//Remove the last comma
        if (send(fd, send_buf.c_str(), send_buf.size(), 0) == -1) {
            flog << "Sending response failed" << std::endl;
            exit(EXIT_FAILURE);
        }
		flog<<"Send "<<send_buf<<std::endl;
		//Clear the receive buffer
		send_buf.clear();
    } else if(strcmp(command, "COMMIT")==0) {
		p0=strtok_r(token, " ", &token);	//Timestamp
		p1=strtok_r(token, " ", &token);	//File name
		p2=strtok_r(token, " ", &token);	//Content

		//Execute write operation
		server_files[p1]<<p2<<" ("<<p0<<")"<<std::endl;

		send_buf="FIN";
        if (send(fd, send_buf.c_str(), send_buf.size(), 0) == -1) {
            flog << "Sending response failed" << std::endl;
            exit(EXIT_FAILURE);
        }
		flog<<"Send "<<send_buf<<std::endl;
		send_buf.clear();
    } else if(strcmp(command, "BYE")==0) {	//Close connection
		if(close(fd)!=0) {
			flog<<"Close connection failed"<<std::endl;
			exit(EXIT_FAILURE);
		}
	}
}

void run_server(int server_id) {
	int sd=-1, opt=-1;
	struct sockaddr_in address;
	char recv_buf[100]={0};
    int addrlen = sizeof(address);
	std::unordered_map<std::string, std::ofstream> server_files;
	int master_socket, new_socket, max_clients=5, activity, i, valread, max_sd;
	std::vector<int> client_socket;
	std::vector<int> server_port={SERVER_0_PORT, SERVER_1_PORT, SERVER_2_PORT};

	client_socket.resize(max_clients);
	//Open the log file. path:log/
    flog_path="log/s"+std::to_string(server_id)+".log";
    flog.open(flog_path);
    flog << "Server " << server_id << " is started" << std::endl;

	//Set server hosted files
	set_file_list(server_id);	
	flog<<"***Server hosted file list***"<<std::endl;
	for(auto f: file_list) {
		flog<<f<<std::endl;
	}
	flog<<"*****************************"<<std::endl;

	//Initialization
	flog<<"Start initialization"<<std::endl;
	for(auto f : file_list) {
		std::string server_file_path="s"+std::to_string(server_id)+"/"+f;		
		server_files[f].open(server_file_path, std::ios_base::app);
	}
	flog<<"Initialization done"<<std::endl;

	//Create a port for each server to communicate with clients
	fd_set readfds;
	flog<<"Reset client_socket"<<std::endl;
	for(int i=0; i<max_clients; i++) {
		client_socket[i]=0;
	}
    if( (master_socket = socket(AF_INET , SOCK_STREAM , 0)) == 0)  
    {  
        perror("socket failed");  
        exit(EXIT_FAILURE);  
    }
	
	//set master socket to allow multiple connections , 
    //this is just a good habit, it will work without this
    if( setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, 
          sizeof(opt)) < 0 )  
    {  
        perror("setsockopt");  
        exit(EXIT_FAILURE);  
    }

	//type of socket created
    address.sin_family = AF_INET;  
    address.sin_addr.s_addr = INADDR_ANY;  
    address.sin_port = htons(server_port[server_id]);

	//bind the socket to localhost port
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address))<0)  
    {  
        perror("bind failed");  
        exit(EXIT_FAILURE);  
    }

	//try to specify maximum of 50 pending connections for the master socket
	flog<<"Listening to port "<<server_port[server_id]<<std::endl;
    if (listen(master_socket, 50) < 0)  
    {  
        perror("listen");  
        exit(EXIT_FAILURE);  
    }

	//accept the incoming connection
	addrlen = sizeof(address);
	while(1) {
		//clear the socket set
		FD_ZERO(&readfds);

		//add master socket to set
        FD_SET(master_socket, &readfds);  
        max_sd = master_socket; 

		//add child sockets to set
        for ( i = 0 ; i < max_clients ; i++)  
        {  
            //socket descriptor 
             sd = client_socket[i];
            
			//if valid socket descriptor then add to read list 
            if(sd > 0)  
            	FD_SET( sd , &readfds);                                                                                      
            //highest file descriptor number, need it for the select function 
            if(sd > max_sd)  
    	        max_sd = sd;
		}

		//wait for an activity on one of the sockets , timeout is NULL , 
		//so wait indefinitely
		activity = select( max_sd + 1 , &readfds , NULL , NULL , NULL);

        if ((activity < 0) && (errno!=EINTR))  
        {  
            printf("select error");  
        }
		//If something happened on the master socket ,
		//then its an incoming connection
        if (FD_ISSET(master_socket, &readfds))  
        {  
            if ((new_socket = accept(master_socket, 
                    (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)  
            {  
                perror("accept");  
                exit(EXIT_FAILURE);  
            }
			
			//inform user of socket number - used in send and receive commands
			int port=ntohs(address.sin_port);
            flog<<"New connection , socket fd is "<<std::to_string(new_socket)<<", port : "<<port<<std::endl;

			for(int i=0; i<max_clients; i++) {
				if(client_socket[i]==0) {
					client_socket[i]=new_socket;
					break;
				}
			}

			if (read(new_socket, recv_buf, sizeof(recv_buf)) == -1) {
        		flog << "read failed" << std::endl;
            	exit(EXIT_FAILURE);
	        }
			process_message(new_socket, server_id, recv_buf, std::ref(flog), std::ref(server_files));
		}

		//else its some IO operation on some other socket
    	for (i = 0; i < max_clients; i++)  
	    {
    	    sd = client_socket[i];
        	if (FD_ISSET( sd , &readfds))  
	        {  
				if (read(sd, recv_buf, sizeof(recv_buf)) == -1) {
        			flog << "read failed" << std::endl;
            		exit(EXIT_FAILURE);
	        	}
				process_message(sd, server_id, recv_buf, std::ref(flog), std::ref(server_files));
				bzero(recv_buf, sizeof(recv_buf));
			}
		}//end for
	}//end while


	//Close files
	for(auto f : file_list) {
		server_files[f].close();
	}
	flog<<"Files closed"<<std::endl;
	flog.close();
}

