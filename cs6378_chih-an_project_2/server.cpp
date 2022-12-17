#include "ricart.h"

static thread_local std::ofstream flog;//For logging
static thread_local std::string flog_path;
static thread_local std::unordered_map<std::string, int> local_ts;
static thread_local message pending_req;//The current processing request received from client
static std::vector<std::vector<int>> fd_list(3, std::vector<int>(3,-1));//Ex. [s0][s1]: File descriptor for server_0 which connects to server_1
static thread_local std::vector<std::string> file_list;//Server hosted files
static thread_local std::unordered_map<std::string, int> outstd_ack_cnt;//ACK is sent when the broadcast operation is done
static thread_local std::mutex mtx;
static thread_local std::unordered_map<std::string, bool> write_fin_flag;//Tell client the WRITE operation is finished
static thread_local std::unordered_map<std::string, std::vector<bool>> deferred_reply;//List for recording the deferred replies
static thread_local std::unordered_map<std::string, int> highest_ts;
static thread_local std::unordered_map<std::string, bool> enter_cs;//Indicate if the server is in the critical section
static thread_local std::unordered_map<std::string, std::vector<bool>> received_reply_list;//Record the received reply for optimization
static thread_local std::unordered_map<std::string, bool> waiting_for_replies;//Indicate that the server is waiting for other servers' replies

//Set server hosted files to the list
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

//Connect to a specific socket
int connect_socket(int &fd, std::string ip_addr, int port_num) {
	struct sockaddr_in serv_addr;

    //Create a socket for communication with clients
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cout << "socket failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port_num);
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0) {
    	flog << std::string("Invalid address/ Address not supported") << std::endl;
        exit(EXIT_FAILURE);
    }
	flog<<"Trying to connect to "<<ip_addr<<":"<<port_num<<std::endl;
	while(1) {
    	if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0) {
        	break;
   		}
	}
	flog<<"Connect successfully"<<std::endl;
    return 0;
}

//Create a socket and listen to it
int create_sock(int &fd, int port_num) {
	struct sockaddr_in addr;
	int opt=-1;
	int addrlen=sizeof(addr);

    //Create a socket for communication with clients
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cout << "socket failed" << std::endl;
        exit(EXIT_FAILURE);
    }
	
    /* Configure socket settings */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        flog << "setsockopt failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_num);

    /* Forcefully attach socket to the port */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr))<0) {
        flog << "bind failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    /* Listen for connections on the socket */
    if (listen(fd, 10) < 0) {
        flog << "listen failed" << std::endl;
        exit(EXIT_FAILURE);
    }

	flog<<"Listining to port: "<<port_num<<"..."<<std::endl;
	return 0;
}

//Send broadcast to other servers for file synchronization
void send_broadcast(int server_id, std::string file_name, message m, std::ofstream &flog, int ts) {
	thread_local std::string send_buf;

	for(int i=0; i<3; i++) {
		if(i!=server_id) {
			send_buf="BC "+std::to_string(ts)+" "+file_name+" "+m.content+"$#";
            if (send(fd_list[server_id][i], send_buf.c_str(), send_buf.size(), 0) == -1) {
                flog << "Sending response failed" << std::endl;
                exit(EXIT_FAILURE);
            }
			flog<<"Send "<<send_buf<<", server:"<<server_id<<"->"<<i<<std::endl;
		}
	}
}

//Determine if the process need to request mutual exclusion
bool need_me(std::string file_name, message &pending_req) {
	return ((pending_req.file_name).compare(file_name)==0 && pending_req.timestamp!=-1);
}

//Process used to check the need for mutual exclusion periodically
//Implementation for Ricart-Agrawala
void req_me(int server_id, std::string file_name, std::mutex &mtx, std::unordered_map<std::string, int> &local_ts, std::unordered_map<std::string, int> &outstd_ack_cnt, std::unordered_map<std::string, bool> &write_fin_flag, std::unordered_map<std::string, std::vector<bool>> &deferred_reply, message &pending_req, std::ofstream &flog, std::unordered_map<std::string, bool> &enter_cs, std::unordered_map<std::string, std::ofstream> &server_files, std::unordered_map<std::string, int> &highest_ts, std::unordered_map<std::string, std::vector<bool>> &received_reply_list, std::unordered_map<std::string, bool> &waiting_for_replies) {
	thread_local std::string send_buf;

	while(1) {
		if(need_me(file_name, pending_req)) {//Received a request from client for <file_name> file
			std::string file_path="s"+std::to_string(server_id)+"/"+file_name;
			message m=pending_req;

			waiting_for_replies[file_name]=true;
			mtx.lock();
			local_ts[file_name]=std::max(local_ts[file_name], highest_ts[file_name])+1;
			mtx.unlock();
			flog<<"Local timestamp["<<file_name<<"]="<<local_ts[file_name]<<std::endl;
			for(int i=0; i<3; i++) {//Send request for mutual exclusion to other servers
				if(i!=server_id && !received_reply_list[file_name][i]) {//If a reply has been received from a specific server, we dont need this server's reply to enter critical section
					send_buf="REQME "+std::to_string(local_ts[file_name])+" "+std::to_string(server_id)+" "+file_name+"$#";
                	if (send(fd_list[server_id][i], send_buf.c_str(), send_buf.size(), 0) == -1) {
                    	flog << "Sending response failed" << std::endl;
                    	exit(EXIT_FAILURE);
                	}
                	flog <<"Send "<<send_buf<<", server:"<<server_id<<"->"<<i<<std::endl;
				}
			}
			send_buf.clear();
			flog<<"Waiting for replies..."<<std::endl;
			while(1) {
				int j=2;
				for(int i=0; i<3; i++) {
					if(i!=server_id && received_reply_list[file_name][i]) {
						j--;
					}
				}
				if(j==0)
					break;
			}
			waiting_for_replies[file_name]=false;
			//Enter critical section
			enter_cs[file_name]=true;
			flog<<"***Enter CS (file:"<<file_name<<")***"<<std::endl;
			server_files[file_name]<<m.content<<" ("<<std::to_string(pending_req.timestamp)<<")"<<std::endl;
			flog<<"Write '"+m.content+" "+std::to_string(pending_req.timestamp)+"' to "<<file_name<<std::endl;
			mtx.lock();
			outstd_ack_cnt[file_name]=2;
			mtx.unlock();
			send_broadcast(server_id, file_name, m, std::ref(flog), pending_req.timestamp);//Send broadcast to other servers
			flog<<"Waiting for ACKs..."<<std::endl;

			while(outstd_ack_cnt[file_name]!=0);
			flog<<"Receive 2 ACK"<<std::endl;
			write_fin_flag[file_name]=true;
			//Exit critical section
			flog<<"***Exit CS (file:"<<file_name<<")***"<<std::endl;
			enter_cs[file_name]=false;
			mtx.lock();
			pending_req.timestamp=-1;//The pending request from client is done
			pending_req.file_name="";
			mtx.unlock();
			
			//Reply deferred reply
			for(int i=0; i<deferred_reply[file_name].size(); i++) {
				if(i!=server_id && deferred_reply[file_name][i]) {
					std::string send_buf="REPLY "+file_name+"$#";
                	if (send(fd_list[server_id][i], send_buf.c_str(), send_buf.size(), 0) == -1) {
                    	flog << "Sending response failed" << std::endl;
                    	exit(EXIT_FAILURE);
                	}
                	flog <<"Send deferred "<<send_buf<<", server:"<<server_id<<"->"<<i<<std::endl;
					send_buf.clear();
					mtx.lock();
					deferred_reply[file_name][i]=false;
					received_reply_list[file_name][i]=false;
					mtx.unlock();
				}
			}
		}
	}
}

//Process which processes the received messages
void process_request(int server_id, int target_server_id, std::ofstream &flog, std::unordered_map<std::string, int> &local_ts, std::unordered_map<std::string, int> &highest_ts, std::mutex &mtx, std::unordered_map<std::string, int> &outstd_ack_cnt, std::unordered_map<std::string, bool> &enter_cs, std::unordered_map<std::string, std::ofstream> &server_files, std::unordered_map<std::string, std::vector<bool>> &deferred_reply, std::unordered_map<std::string, std::vector<bool>> &received_reply_list, std::unordered_map<std::string, bool> &waiting_for_replies, message &pending_req) {
	thread_local char recv_buf[100]={0};
	thread_local int fd=fd_list[server_id][target_server_id];
    thread_local char *command = NULL;
	thread_local std::string send_buf;
	thread_local int recv_ts;
	thread_local std::string file_name;
	thread_local int req_server_id;
	thread_local bool defer_it;
	thread_local bool our_priority;
	thread_local std::string content;
	thread_local std::string file_path="s"+std::to_string(server_id)+"/";
	thread_local char *token=NULL;
	thread_local char* message;

	while(1) {
		bzero(recv_buf,sizeof(recv_buf));
        if (read(fd, recv_buf, sizeof(recv_buf)) == -1) {
            flog << "read failed" << std::endl;
            exit(EXIT_FAILURE);
        }
		token=recv_buf;

		while(1) {
			message=NULL;
			message=strtok_r(token, "$#", &token);//messages are seperated by $# symbol
			if(!message)
				break;
			flog<<"Receive "<<message<<", server:"<<target_server_id<<"->"<<server_id<<std::endl;
			command=strtok_r(message, " ", &message);
			
			//Process received requests
        	if(strcmp(command, "REQME")==0) {//Implementation for Ricart-Agrawala and Roucairol-Carvalho algorithm
				recv_ts=atoi(strtok_r(message, " ", &message));
				req_server_id=atoi(strtok_r(message, " ", &message));	
				file_name=strtok_r(message, " ", &message);
				highest_ts[file_name]=std::max(local_ts[file_name], highest_ts[file_name]);
				highest_ts[file_name]=std::max(highest_ts[file_name], recv_ts);
				our_priority=recv_ts>local_ts[file_name] || (recv_ts==local_ts[file_name] && req_server_id>server_id);//If we have higher priority, set our_priority to true
				defer_it=enter_cs[file_name] || (waiting_for_replies[file_name] && our_priority);//Indicate that if we should defer the received message
				flog<<"Need to defer? "<<defer_it<<std::endl;
				if(defer_it) {
					mtx.lock();
					deferred_reply[file_name][target_server_id]=true;
					mtx.unlock();
				}
				if((!(waiting_for_replies[file_name] || enter_cs[file_name])) || (waiting_for_replies[file_name] && !received_reply_list[file_name][target_server_id] && !our_priority)) {
					received_reply_list[file_name][target_server_id]=false;//Receive a new request, so we need the corresponding server's REPLY to enter critical section.
					//Send Reply
					send_buf="REPLY "+file_name+"$#";
            		if (send(fd, send_buf.c_str(), send_buf.size(), 0) == -1) {
                		flog << "Sending response failed" << std::endl;
                		exit(EXIT_FAILURE);
            		}
					flog<<"Send "<<send_buf<<", server:"<<server_id<<"->"<<target_server_id<<std::endl;
				}
				if(waiting_for_replies[file_name] && received_reply_list[file_name][target_server_id] && !our_priority) {
					received_reply_list[file_name][target_server_id]=false;
					//Send Reply
					send_buf="REPLY "+file_name+"$#";
            		if (send(fd, send_buf.c_str(), send_buf.size(), 0) == -1) {
                		flog << "Sending response failed" << std::endl;
                		exit(EXIT_FAILURE);
            		}
					flog<<"Send "<<send_buf<<", server:"<<server_id<<"->"<<target_server_id<<std::endl;
					//Send REQME
					send_buf="REQME "+std::to_string(local_ts[file_name]) +" "+std::to_string(server_id)+" "+pending_req.file_name+"$#";
            		if (send(fd, send_buf.c_str(), send_buf.size(), 0) == -1) {
                		flog << "Sending response failed" << std::endl;
                		exit(EXIT_FAILURE);
            		}
					flog<<"Send "<<send_buf<<", server:"<<server_id<<"->"<<target_server_id<<std::endl;
				}
			} else if(strcmp(command, "BC")==0) {//Receive broadcast messages
				//Write the content to the file
				char *ts;
				ts=strtok_r(message, " ", &message);
				file_name=strtok_r(message, " ", &message);
				content=strtok_r(message, " ", &message);
				server_files[file_name]<<content<<" ("<<ts<<")"<<std::endl;
				//Send ACK
				send_buf="ACK "+file_name+"$#";
            	if (send(fd, send_buf.c_str(), send_buf.size(), 0) == -1) {
                	flog << "Sending response failed" << std::endl;
                	exit(EXIT_FAILURE);
           	 	}
				flog<<"Send "<<send_buf<<", server:"<<server_id<<"->"<<target_server_id<<std::endl;
				send_buf.clear();
			} else if(strcmp(command, "ACK")==0) {//Receive the message that another server has finished writing for the broadcast request
				std::string fn=strtok_r(message, " ", &message);
				mtx.lock();
				outstd_ack_cnt[fn]--;
				mtx.unlock();
				flog<<"outstd_ack_cnt="<<outstd_ack_cnt[fn]<<std::endl;
			} else if(strcmp(command, "REPLY")==0) {//Receive another server's admission for entering critical section
				file_name=strtok_r(message, " ", &message);
				received_reply_list[file_name][target_server_id]=true;
			}	
		}
	}
}

void run_server(int server_id) {
	int sfd=-1,opt=-1,new_sfd=-1;
	struct sockaddr_in addr;
	thread_local char recv_buf[100]={0};
	thread_local char* token=NULL;
    std::string send_buf;
    int addrlen = sizeof(addr);
    char *p1, *p2, *p3, *p4;
	std::unordered_map<std::string, std::ofstream> server_files;

    flog_path="log/s"+std::to_string(server_id)+".log";
    flog.open(flog_path);
    flog << "Server " << server_id << " is started" << std::endl;
	set_file_list(server_id);
	
	flog<<"***Server hosted file list***"<<std::endl;
	for(auto f: file_list) {
		flog<<f<<std::endl;
	}
	flog<<"*****************************"<<std::endl;

	flog<<"Start initialization"<<std::endl;
	for(auto f : file_list) {
		std::string server_file_path="s"+std::to_string(server_id)+"/"+f;		

		local_ts[f]=0;
		highest_ts[f]=0;
		deferred_reply[f].resize(3);
		received_reply_list[f].resize(3);
		write_fin_flag[f]=false;
		enter_cs[f]=false;
		server_files[f].open(server_file_path, std::ios_base::app);
		for(int i=0; i<3; i++) {
			deferred_reply[f][i]=false;
			received_reply_list[f][i]=false;
		}
	}
	pending_req.timestamp=-1;//Timestamp equals to -1 means there is no pending WRITE request
	flog<<"Initialization done"<<std::endl;

	//Set connections between servers
	switch(server_id) {
		case 0:
			create_sock(sfd, SERVER_0_SERVER_PORT);
    		if ((fd_list[0][1] = accept(sfd, (struct sockaddr *)&addr, (socklen_t*)&addrlen))<0) {
       			flog << "accept failed" << std::endl;
        		exit(EXIT_FAILURE);
    		}
			std::cout<<"s0<->s1 connection established"<<std::endl;
	        connect_socket(fd_list[0][2], "127.0.0.1", SERVER_2_SERVER_PORT);
			break;
		case 1:
	        connect_socket(fd_list[1][0], "127.0.0.1", SERVER_0_SERVER_PORT);
			create_sock(sfd, SERVER_1_SERVER_PORT);
    		if ((fd_list[1][2] = accept(sfd, (struct sockaddr *)&addr, (socklen_t*)&addrlen))<0) {
       			flog << "accept failed" << std::endl;
        		exit(EXIT_FAILURE);
    		}
			std::cout<<"s1<->s2 connection established"<<std::endl;
			break;
		case 2:
			create_sock(sfd, SERVER_2_SERVER_PORT);
    		if ((fd_list[2][0] = accept(sfd, (struct sockaddr *)&addr, (socklen_t*)&addrlen))<0) {
       			flog << "accept failed" << std::endl;
        		exit(EXIT_FAILURE);
    		}
			std::cout<<"s0<->s2 connection established"<<std::endl;
	        connect_socket(fd_list[2][1], "127.0.0.1", SERVER_1_SERVER_PORT);
			break;
	}

	flog<<"Starting daemon threads..."<<std::endl;
	//Start daemon thread for invoking mutual exclusion
	std::thread it[3];
	int j=0;

	for(auto f : file_list) {
		if(j>2)
			break;
		it[j++]=std::thread(req_me, server_id, f, std::ref(mtx), std::ref(local_ts), std::ref(outstd_ack_cnt), std::ref(write_fin_flag), std::ref(deferred_reply), std::ref(pending_req), std::ref(flog), std::ref(enter_cs), std::ref(server_files), std::ref(highest_ts), std::ref(received_reply_list), std::ref(waiting_for_replies));
	}
	//Start daemon thread for receiving messages
	std::thread rqt[2];
	j=0;

	for(int i=0; i<3; i++) {
		if(i!=server_id) {
			rqt[j++]=std::thread(process_request, server_id, i, std::ref(flog), std::ref(local_ts),  std::ref(highest_ts), std::ref(mtx), std::ref(outstd_ack_cnt), std::ref(enter_cs), std::ref(server_files), std::ref(deferred_reply), std::ref(received_reply_list), std::ref(waiting_for_replies), std::ref(pending_req));
		}
	}
	flog<<"Daemon threads start successfully"<<std::endl;

	//Create a port for processing client's requests
    switch(server_id) {
        case 0:
            create_sock(sfd, SERVER_0_PORT);
            break;
        case 1:
            create_sock(sfd, SERVER_1_PORT);
            break;
        case 2:
			create_sock(sfd, SERVER_2_PORT);
			break;
	}	

    while(1) {
		flog<<"Start listening to client's requests..."<<std::endl;
        //Accept client connection on the socket
        if ((new_sfd = accept(sfd, (struct sockaddr *)&addr, (socklen_t*)&addrlen))<0) {
            flog << "accept failed" << std::endl;
            exit(EXIT_FAILURE);
        }
        flog << "Connection established" << std::endl;
        flog << "Receiving messages..." << std::endl;
        while (1) {
            if (read(new_sfd, recv_buf, sizeof(recv_buf)) == -1) {
                flog << "read failed" << std::endl;
                exit(EXIT_FAILURE);
            }
            char *command = NULL,*p1 = NULL, *p2 = NULL;
			flog<<"Receive "<<recv_buf<<" from client"<<std::endl;
			token=recv_buf;
            command = strtok_r(token, " ", &token);

			//Process the received command
            if(strcmp(command, "BYE")==0) {
			    if (close(new_sfd) != 0) {
    			    flog<<"close failed"<<std::endl;
					exit(EXIT_FAILURE);
		   		}
			    flog<<"Connection closed"<<std::endl;
                break;
            } else if(strcmp(command, "ENQUIRY")==0) {
                DIR *dir;
                struct dirent *ent;
                const char *dir_path=("s"+std::to_string(server_id)+"/").c_str();

				for(auto f : file_list) {
					send_buf=send_buf+f+",";
				}
				send_buf=send_buf.substr(0,send_buf.size()-1);//Remove the last comma
                if (send(new_sfd, send_buf.c_str(), send_buf.size(), 0) == -1) {
                    flog << "Sending response failed" << std::endl;
                    exit(EXIT_FAILURE);
                }
				flog<<"Send "<<send_buf<<std::endl;
				//Clear the receive buffer
				bzero(recv_buf,sizeof(recv_buf));
				send_buf.clear();
            } else if(strcmp(command, "WRITE")==0) {
				message m;
				int rec_ts;
				m.timestamp=atoi(strtok_r(token, " ", &token));
				m.file_name=strtok_r(token, " ", &token);
                m.content=strtok_r(token, " ", &token);//content to append
				mtx.lock();
				local_ts[m.file_name]=m.timestamp;
				pending_req=m;
				mtx.unlock();
				while(!write_fin_flag[m.file_name]);

				//WRITE complete, send OK response to client
				send_buf="OK "+std::to_string(highest_ts[m.file_name]);
                if (send(new_sfd, send_buf.c_str(), send_buf.size(), 0) == -1) {
                    flog << "Sending response failed" << std::endl;
                    exit(EXIT_FAILURE);
                }
                flog <<"Send "<<send_buf<<std::endl;
				write_fin_flag[m.file_name]=false;
				bzero(recv_buf,sizeof(recv_buf));
				send_buf.clear();
			}
		}
	}

	for(int i=0; i<3; i++) {
		it[i].join();
	}
	for(int i=0; i<2; i++) {
		rqt[i].join();
	}
	//Close connection between servers
	for(int i=0; i<3; i++) {
		if (close(fd_list[server_id][i]) != 0) {
    		flog<<"close failed"<<std::endl;
			exit(EXIT_FAILURE);
		}
		flog<<"Connection closed"<<std::endl;
	}

	//Close files
	for(auto f : file_list) {
		server_files[f].close();
	}
	flog<<"Files closed"<<std::endl;
	flog.close();
}

