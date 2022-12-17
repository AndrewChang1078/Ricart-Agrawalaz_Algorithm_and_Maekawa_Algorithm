#include "maekawa.h"

static thread_local std::ofstream flog;
static thread_local int client_id;
static thread_local std::vector<int> server_port={SERVER_0_PORT, SERVER_1_PORT, SERVER_2_PORT};
static thread_local std::vector<std::string> file_list;
static std::vector<int> vClientPorts={CLIENT_0_PORT, CLIENT_1_PORT, CLIENT_2_PORT, CLIENT_3_PORT, CLIENT_4_PORT};
std::vector<std::string> mt={"REQUEST", "LOCKED", "INQUIRE", "FAILED", "RELINQUISH", "RELEASE"};
static thread_local std::vector<int> v_server_fd_list;
static thread_local maekawa_control_info minfo;
static thread_local std::vector<std::string> server_ip_list={"10.176.69.39", "10.176.69.40", "10.176.69.41"};
static thread_local std::vector<std::string> client_ip_list={"10.176.69.42", "10.176.69.43", "10.176.69.44", "10.176.69.45", "10.176.69.46"};

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
    if(inet_pton(AF_INET, ip_addr.c_str(), &serv_addr.sin_addr)<=0) {
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

//Dump the waiting requests queue
void dump_waiting_queue(std::ofstream &flog, std::unordered_map<std::string, std::vector<std::vector<int>>> vReceivedRequests, std::string file_name) {
	for(auto a:vReceivedRequests[file_name]) {
		for(int i=0; i<2; i++) {
			if(i==0)
				flog<<"timestamp:"<<a[i]<<std::endl;
			else
				flog<<"process id:"<<a[i]<<std::endl;
		}
		flog<<std::endl;
	}
}

//Processes the received messages from other clients
void process_message(int client_id, std::string m, std::ofstream &flog, maekawa_control_info &minfo) {
	message_type type;
	int id;
	int ts;
	std::string file_name;
	 message_type t;
	 char *message;

	//Parse the message
	message=(char*)m.c_str();
	type=(message_type)atoi(strtok_r(message, " ", &message));
	id=atoi(strtok_r(message, " ", &message));
	ts=atoi(strtok_r(message, " ", &message));
	file_name=strtok_r(message, " ", &message);
	minfo.highest_ts[file_name]=std::max(minfo.highest_ts[file_name], ts);
	flog<<"Receive "<<mt[type]<<" from client "<<std::to_string(id)<<", ts= "<<std::to_string(ts)<<", file_name= "<<file_name<<std::endl;

	if(type==REQUEST) {	//Receive REQUEST message
		if(!minfo.is_locked[file_name]) {	//If client is not currently locked for other nodes, locks it
			t=LOCKED;
			send_message(t, client_id, id, file_name, std::ref(flog), 0, 0, std::ref(minfo));
			minfo.is_locked[file_name]=true;
			minfo.cur_lock_proc[file_name]=id;
			minfo.cur_lock_proc_ts[file_name]=ts;
			flog<<"Lock for client "<<std::to_string(id)<<", file_name= "<<file_name<<std::endl;
		} else {	//If client is currently locked
			//Push new request to the queue and sort the queue
			minfo.vReceivedRequests[file_name].push_back({ts, id});
			std::sort(minfo.vReceivedRequests[file_name].begin(), minfo.vReceivedRequests[file_name].end());
			flog<<"Dump Waiting Queue(after sort)"<<std::endl;
			dump_waiting_queue(std::ref(flog), minfo.vReceivedRequests, file_name);

			//If new request has lower priority, send back FAILED
			if((minfo.vReceivedRequests[file_name][0][0]<ts || (minfo.vReceivedRequests[file_name][0][0]==ts && minfo.vReceivedRequests[file_name][0][1]<id)) || (minfo.cur_lock_proc_ts[file_name]<ts || (minfo.cur_lock_proc_ts[file_name]==ts && minfo.cur_lock_proc[file_name]<id))) {	//Any precede request has high priority than received request
				t=FAILED;
				send_message(t, client_id, id, file_name, std::ref(flog), 0, 0, std::ref(minfo));
			}else if(!minfo.is_inquire_sent[file_name]) {	//The new request has highest priority
				//If INQUIRE has not been sent, send INQUIRE
				t=INQUIRE;
				send_message(t, client_id, minfo.cur_lock_proc[file_name], file_name, std::ref(flog), 0, 0, std::ref(minfo));
				minfo.is_inquire_sent[file_name]=true;
			}
		}
	} else if(type==INQUIRE) {	//Receive INQUIRE, need to reply RELINQUISH or RELEASE
		if(minfo.is_received_failed[file_name] && !minfo.can_enter_cs[file_name]) {	//If client has received a FAILED before, reply RELINQUISH
			t=RELINQUISH;
			send_message(t, client_id, id, file_name, std::ref(flog), 0, 0, std::ref(minfo));
		} else {
			//Not sure if client can lock all quorem members yet, push it to wait-to-respond queue
			minfo.unrespond_inquire_proc[file_name].push_back(id);
		}
	} else if(type==RELINQUISH) {	//Receive RELINQUISH message
		//Unlock itself and lock for the highest priority request in the waiting queue
		minfo.is_locked[file_name]=false;
		minfo.vReceivedRequests[file_name].push_back({minfo.cur_lock_proc_ts[file_name], minfo.cur_lock_proc[file_name]});
		std::sort(minfo.vReceivedRequests[file_name].begin(), minfo.vReceivedRequests[file_name].end());
		if(minfo.vReceivedRequests[file_name].size()>0) {	//If there is any waiting requests
			minfo.is_locked[file_name]=true;
			minfo.cur_lock_proc[file_name]=minfo.vReceivedRequests[file_name][0][1];
			minfo.cur_lock_proc_ts[file_name]=minfo.vReceivedRequests[file_name][0][0];
			minfo.vReceivedRequests[file_name].erase(minfo.vReceivedRequests[file_name].begin());
			t=LOCKED;
			send_message(t, client_id, minfo.cur_lock_proc[file_name], file_name, std::ref(flog), 0, 0, std::ref(minfo));
			flog<<"[RELINQUISH]Lock for client "<<std::to_string(minfo.cur_lock_proc[file_name])<<", file_name= "<<file_name<<std::endl;
		}
		//The INQUIRE has been responded
		minfo.is_inquire_sent[file_name]=false;
	} else if(type==LOCKED) {	//Receive a LOCKED message
		int grant_count=0;
		minfo.quorem_grant[file_name][id]=true;	//Set quorem member vote to true

		//Check if all quorem members vote for the client
		for(auto q:minfo.quorem_list) {
			if(minfo.quorem_grant[file_name][q]) {
				flog<<"Client:"<<q<<" grants"<<std::endl;
				grant_count++;
			}
		}

		//If all members vote yes, allow entering C.S.
		if(grant_count==QUOREM_MEMBER_COUNT) {
			flog<<"Can enter C.S now"<<std::endl;
			minfo.can_enter_cs[file_name]=true;
		}
	} else if(type==RELEASE) {
		//INQUIRE is reponded
		minfo.is_inquire_sent[file_name]=false;

		//unlock itself
		minfo.is_locked[file_name]=false;
		for(int i=0; i<minfo.vReceivedRequests[file_name].size(); i++) {	//Remove current request from waiting queue
			if(minfo.vReceivedRequests[file_name][i][0]==minfo.cur_lock_proc_ts[file_name] && minfo.vReceivedRequests[file_name][i][1]==minfo.cur_lock_proc[file_name]) {
				minfo.vReceivedRequests[file_name].erase(minfo.vReceivedRequests[file_name].begin()+i);
			}
		}
		if(minfo.vReceivedRequests[file_name].size()>0) {	//If waiting queue is not empty, lock for the next highest priority request
			minfo.is_locked[file_name]=true;
			minfo.cur_lock_proc[file_name]=minfo.vReceivedRequests[file_name][0][1];
			minfo.cur_lock_proc_ts[file_name]=minfo.vReceivedRequests[file_name][0][0];
			t=LOCKED;
			send_message(t, client_id, minfo.cur_lock_proc[file_name], file_name, std::ref(flog), 0, 0, std::ref(minfo));
			minfo.vReceivedRequests[file_name].erase(minfo.vReceivedRequests[file_name].begin());
		} else {	//Waiting queue is empty, unlock itself
			minfo.is_locked[file_name]=false;
		}
	} else if(type==FAILED) {	//Receive FAILED message
		minfo.is_received_failed[file_name]=true;
		//If INQUIRE is not responded yet, reply FAILED
		if(minfo.unrespond_inquire_proc.count(file_name)>0) {
			for(auto a: minfo.unrespond_inquire_proc[file_name]) {
				t=RELINQUISH;
				send_message(t, client_id, a, file_name, std::ref(flog), 0, 0, std::ref(minfo));
			}
			//INQUIRE has been responded, remove it from the queue
			minfo.unrespond_inquire_proc[file_name].erase(minfo.unrespond_inquire_proc[file_name].begin(), minfo.unrespond_inquire_proc[file_name].end());
		}
	}
}

//Send message to a node
void send_message(message_type t, int from_client_id, int to_client_id, std::string file_name, std::ofstream &flog, bool fix_ts, int given_ts, maekawa_control_info &minfo) {
	int fd=-1;
	std::string send_buf;
	int bc_ts;

	if(from_client_id!= to_client_id && minfo.client_socket[to_client_id]==0) {
		if(connect_socket(fd, client_ip_list[to_client_id], vClientPorts[to_client_id])<0) {
			flog<<"Connect failed"<<std::endl;
			exit(EXIT_FAILURE);
		}
		(minfo.client_socket)[to_client_id]=fd;
		flog<<"Connect successfully, fd= "<<fd<<std::endl;
	}

	//If it is not a broadcast message, we dont need to send by the same timestamp
	if(!fix_ts) {
		flog<<"Send "<<mt[t]<<" to client "<<to_client_id<<", ts= "<<minfo.local_ts[file_name]<<", file= "<<file_name<<std::endl;
		send_buf=std::to_string(t)+" "+std::to_string(from_client_id)+" "+std::to_string(minfo.local_ts[file_name])+" "+file_name+"$#";//Request message: <message type> <from client id> <localtimestamp> <file_name>
	} else {
		flog<<"Send "<<mt[t]<<" to client "<<to_client_id<<", ts= "<<given_ts<<", file= "<<file_name<<std::endl;
		send_buf=std::to_string(t)+" "+std::to_string(from_client_id)+" "+std::to_string(given_ts)+" "+file_name+"$#";//Request message: <message type> <from client id> <localtimestamp> <file_name>
	}

	//If it is a message sent to itself, directly call process_message
	if(from_client_id==to_client_id) {
		send_buf=send_buf.substr(0, send_buf.size()-2);	//Remove delimeter
		process_message(from_client_id, send_buf, std::ref(flog), std::ref(minfo));
	} else if(send(minfo.client_socket[to_client_id], send_buf.c_str(), send_buf.size(), 0) == -1) {	//Otherwise, send it through socket
    	flog << "Sending request failed" << std::endl;
        exit(EXIT_FAILURE);
    }
}

//A daemon for receiving new connections and monitoring IO operations on sockets
void process_connections(int client_id, std::ofstream &flog, maekawa_control_info &minfo) {
	int sd=-1;
	struct sockaddr_in address;
	int addrlen=sizeof(address);
	char recv_buf[100];
	message_type type;
	int opt=1;
	int master_socket, new_socket, max_clients=5, activity, i, valread, max_sd;

	fd_set readfds;
	flog<<"Reset client_socket"<<std::endl;
	for(int i=0; i<max_clients; i++) {
		minfo.client_socket[i]=0;
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
    address.sin_port = htons(vClientPorts[client_id]);

    //bind the socket to localhost port
    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address))<0)  
    {  
        perror("bind failed");  
        exit(EXIT_FAILURE);  
    }

	//try to specify maximum of 50 pending connections for the master socket
	flog<<"Listening to port "<<vClientPorts[client_id]<<std::endl;
    if (listen(master_socket, 50) < 0)  
    {  
        perror("listen");  
        exit(EXIT_FAILURE);  
    }

    //accept the incoming connection 
    addrlen = sizeof(address);  

	bool flag=false;
	while(1) {
        //clear the socket set 
        FD_ZERO(&readfds);  

        //add master socket to set 
        FD_SET(master_socket, &readfds);  
        max_sd = master_socket; 

		//Server 0 is the initiator. It need to wait until hello messages are sent.
		if(client_id==0 && !flag) {
		//if(!flag) {
			sleep(60);
			flag=true;
		}

        //add child sockets to set 
        for ( i = 0 ; i < max_clients ; i++)  
        {  
            //socket descriptor 
            sd = minfo.client_socket[i];
                 
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
           
			if (read(new_socket, recv_buf, sizeof(recv_buf)) == -1) {
        			flog << "read failed" << std::endl;
            		exit(EXIT_FAILURE);
       		}
			char *m, *t;
			//flog<<"Receive "<<recv_buf<<std::endl;
			m=recv_buf;
       		t=strtok_r(m, " ", &m);
			if(strcmp(t, "HELLOFROM")!=0) {
				flog<<"Need HELLO first"<<std::endl;
				exit(EXIT_FAILURE);
			}

			//Process the received command
			int id;
			id=atoi(strtok_r(m, " ", &m));

			//Add new connection fd to corresponding client fd list
			if(id!=client_id) {
				minfo.client_socket[id] = new_socket;  
				flog<<"client_socket["<<std::to_string(id)<<"]="<<std::to_string(new_socket)<<std::endl;
			}
			bzero(recv_buf, sizeof(recv_buf));
        } 

        //else its some IO operation on some other socket
        for (i = 0; i < max_clients; i++)  
        {  
            sd = minfo.client_socket[i];  
            //flog<<"sd= "<<sd<<std::endl;    
            if (FD_ISSET( sd , &readfds))  
            { 
            	//flog<<"sd= "<<sd<<std::endl;     
				if (read(sd, recv_buf, sizeof(recv_buf)) == -1) {
        			flog << "111read failed" << std::endl;
            		exit(EXIT_FAILURE);
        		}
				char *message;
				char *token;
				//flog<<"Receive "<<recv_buf<<std::endl;
				token=recv_buf;

				while(1) {
					message=NULL;
					message=strtok_r(token, "$#", &token);	//Commands are seperated by $#
					if(!message)
						break;
					process_message(client_id, message, std::ref(flog), std::ref(minfo));
				} 
				bzero(recv_buf, sizeof(recv_buf));
			}
        }//end for
	}//end while	
}

//One-time message which is used to build connection between clients in the beginning
void send_hello(int from_client_id, int to_client_id, std::vector<int> &client_socket, std::ofstream &flog) {
	int fd=-1;
	std::string send_buf;

	send_buf="HELLOFROM "+std::to_string(from_client_id);
	if(connect_socket(fd, client_ip_list[to_client_id], vClientPorts[to_client_id])<0) {
	   flog<<"Connect failed"<<std::endl;
	    exit(EXIT_FAILURE);
	}
	client_socket[to_client_id]=fd;
	flog<<"[send_hello]client_socket["<<std::to_string(to_client_id)<<"]="<<std::to_string(fd)<<std::endl;
	if (send(fd, send_buf.c_str(), send_buf.size(), 0) == -1) {
	    flog<<"send failed"<<std::endl;
   		exit(EXIT_FAILURE);
	}
	flog<<"Send HELLO message "<<std::to_string(from_client_id)<<"->"<<std::to_string(to_client_id)<<std::endl;
	send_buf.clear();
}

//Main function for client
void run_client(int id) {
	int fd=-1;
	struct sockaddr_in addr;
	int addrlen=sizeof(addr);

	client_id=id;
	flog.open("log/c"+std::to_string(id)+".log");
	std::cout<<"Client "<<client_id<<" is running..."<<std::endl;

	//Initialization
	flog<<"Start Initialization"<<std::endl;
	file_list={"a.txt", "b.txt", "c.txt"};
	for(auto f:file_list) {
		minfo.local_ts[f]=0;
		minfo.is_locked[f]=false;
		minfo.is_inquire_sent[f]=false;
		minfo.is_received_failed[f]=false;
		minfo.can_enter_cs[f]=false;
		minfo.quorem_grant[f].resize(5);
		for(int i=0; i<5; i++) {
			minfo.quorem_grant[f][i]=false;
		}
	}
	minfo.client_socket.resize(5);
	for(int i=0; i<5; i++) {
		minfo.client_socket[i]=0;
	}
	minfo.quorem_list.push_back(client_id);
	minfo.quorem_list.push_back((client_id+1)%5);
	minfo.quorem_list.push_back((client_id+2)%5);
	flog<<"Initialization done"<<std::endl;

	//Start a daemon thread for processing connections and messages
	std::thread p(process_connections, client_id, std::ref(flog), std::ref(minfo));
	flog<<"111"<<std::endl;
	sleep(30);	//Wait for all clients' daemon ready
	flog<<"222"<<std::endl;

	//Send HELLOFROM messages to build connection with other clients
	switch(client_id) {
		case 0:
			send_hello(0, 1, std::ref(minfo.client_socket), std::ref(flog));
			send_hello(0, 2, std::ref(minfo.client_socket), std::ref(flog));
			send_hello(0, 3, std::ref(minfo.client_socket), std::ref(flog));
			send_hello(0, 4, std::ref(minfo.client_socket), std::ref(flog));
			break;
		case 1:
			send_hello(1, 2, std::ref(minfo.client_socket), std::ref(flog));
			send_hello(1, 3, std::ref(minfo.client_socket), std::ref(flog));
			send_hello(1, 4, std::ref(minfo.client_socket), std::ref(flog));
			break;
		case 2:
			send_hello(2, 3, std::ref(minfo.client_socket), std::ref(flog));
			send_hello(2, 4, std::ref(minfo.client_socket), std::ref(flog));
			break;
		case 3:
			send_hello(3, 4, std::ref(minfo.client_socket), std::ref(flog));
			break;
		case 4:
			break;
	}

	//Wait for all connection between clients established
	flog<<"aaa"<<std::endl;
	sleep(15);
	flog<<"bbb"<<std::endl;

	//Execute different write operations for each client
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

	while(1);
	flog<<"thread join"<<std::endl;
	p.join();	
}

void client0_op() {
	client_write("a.txt", "client0");
	client_write("b.txt", "client0");
	client_write("a.txt", "client0_2");
	client_write("b.txt", "client0_2");
}

void client1_op() {
	client_write("c.txt", "client1");
	client_write("b.txt", "client1");
	client_write("c.txt", "client1_2");
	client_write("a.txt", "client1");
}

void client2_op() {
	client_write("a.txt", "client2");
	client_write("c.txt", "client2");
	client_write("a.txt", "client2_2");
	client_write("b.txt", "client2");
}

void client3_op() {
	client_write("c.txt", "client3");
	client_write("b.txt", "client3");
	client_write("b.txt", "client3_2");
	client_write("a.txt", "client3");
}

void client4_op() {
	client_write("c.txt", "client4");
	client_write("b.txt", "client4");
	client_write("a.txt", "client4");
	client_write("a.txt", "client4_2");
}

void client_write(std::string file_name,std::string c) {
	message_type mt;
	int fix_ts;
	std::string send_buf;
	char recv_buf[500]={0};
	int agree_cnt, fin_cnt;
	bool abort=1;

	//New request begins. Initialize control variables
	minfo.is_received_failed[file_name]=false;

	//Send REQUEST to all members of the quorem
	minfo.local_ts[file_name]=std::max(minfo.highest_ts[file_name], minfo.local_ts[file_name])+1;
	fix_ts=minfo.local_ts[file_name];
	for(auto i:minfo.quorem_list) {
		mt=REQUEST;
		send_message(mt, client_id, i, file_name, std::ref(flog), 1, fix_ts, std::ref(minfo));
	}

	//Waiting for all quorem members' agreement
	while(!minfo.can_enter_cs[file_name]);

	//Enter C.S.
	flog<<"******** ENTER C.S. ******** (file:"<<file_name<<")"<<std::endl;

	//Connect to all the servers at the same time
	if(v_server_fd_list.size()==0) {
		for(int i=0; i<3; i++) {
			int fd=-1;
			connect_socket(fd, server_ip_list[i], server_port[i]);
			v_server_fd_list.push_back(fd);
		}
	}

	//Send message to query servers' will to perform the write
	send_buf="REQ";
	for(int i=0; i<3; i++) {
		if (send(v_server_fd_list[i], send_buf.c_str(), send_buf.size(), 0) == -1) {
			flog<<"send failed"<<std::endl;
			exit(EXIT_FAILURE);
		}
	}

	//Wait for response
	agree_cnt=0;
	while(1) {
		for(int i=0; i<3; i++) {
			if (read(v_server_fd_list[i], recv_buf, sizeof(recv_buf)) == -1) {
		    	flog<<"read failed"<<std::endl;
	    		exit(EXIT_FAILURE);
			}
			flog<<"Receive "<<recv_buf<<std::endl;
			if(strcmp(recv_buf, "OK")==0) {
				agree_cnt++;
			} else if(strcmp(recv_buf, "ABORT")==0) {
				flog<<"Receive ABORT from server"<<std::endl;
				exit(EXIT_FAILURE);
			} else {
				flog<<"Unknown command"<<std::endl;
				exit(EXIT_FAILURE);
			}
			bzero(recv_buf, sizeof(recv_buf));
		}
		//If collect 3 AGREEs, commit 
		if(agree_cnt==3)
			break;
	}	
	
	//Send COMMIT to servers
	send_buf="COMMIT "+std::to_string(minfo.local_ts[file_name])+" "+file_name+" "+c;
	for(int i=0; i<3; i++) {
		if (send(v_server_fd_list[i], send_buf.c_str(), send_buf.size(), 0) == -1) {
			flog<<"send failed"<<std::endl;
			exit(EXIT_FAILURE);
		}
	}

	//Wait until all servers write finished
	fin_cnt=0;
	while(1) {
		for(int i=0; i<3; i++) {
			if (read(v_server_fd_list[i], recv_buf, sizeof(recv_buf)) == -1) {
		    	flog<<"read failed"<<std::endl;
	    		exit(EXIT_FAILURE);
			}
			flog<<"Receive "<<recv_buf<<std::endl;
			if(strcmp(recv_buf, "FIN")==0) {
				fin_cnt++;
			} else {
				flog<<"Unknown command"<<std::endl;
				exit(EXIT_FAILURE);
			}
		}
		//If collect 3 FINISHs, commit 
		if(fin_cnt==3)
			break;
	}
	
#if 0
	//Disconnect to the servers
	for(int i=0; i<3; i++) {
		send_buf="BYE";
		flog<<"Send BYE"<<std::endl;
		if (send(v_server_fd_list[i], send_buf.c_str(), send_buf.size(), 0) == -1) {
			flog<<"send failed"<<std::endl;
			exit(EXIT_FAILURE);
		}
	}
	v_server_fd_list.clear();
#endif

	//Exit C.S.
	flog<<"******** EXIT C.S. ******** (file:"<<file_name<<")"<<std::endl;
	std::cout<<"Client "<<std::to_string(client_id)<<" writes "<<c<<" on "<<file_name<<std::endl;
	
	//Reset control variables
	minfo.unrespond_inquire_proc[file_name].erase(minfo.unrespond_inquire_proc[file_name].begin(), minfo.unrespond_inquire_proc[file_name].end());
	minfo.can_enter_cs[file_name]=false;
	for(int i=0; i<5; i++) {
		minfo.quorem_grant[file_name][i]=false;
	}
	//Send RELEASE message to the members of qeorem
	mt=RELEASE;
	for(auto i:minfo.quorem_list) {
		send_message(mt, client_id, i, file_name, std::ref(flog), 0, 0, std::ref(minfo));
	}
}
