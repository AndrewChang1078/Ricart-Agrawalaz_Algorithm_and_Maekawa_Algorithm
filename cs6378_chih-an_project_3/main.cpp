/*
 * CS6378 HW2
 * Chih-An Chang
 * CXC210017
 * Ricart-Agrawala Algorithm Implementation
 */

#include "maekawa.h"

int main() {
	std::string tmp;
	int id;
	int type;
	//std::thread t4;
	//std::thread t5;
	//std::thread t6;
	//std::thread t7;
	//std::thread t8;

	tmp="Starting servers...";
    //std::cout << tmp << std::endl;
	std::cout<<"Start server(0) or client(1)?"<<std::endl;
	std::cin>>type;
	std::cout<<"Server/Client number=?"<<std::endl;
	std::cin>>id;
	std::cout<<"id= "<<id<<std::endl;

	if(!type)
		run_server(id);
	run_client(id);

    /* Starting servers (0~2) */
    //std::thread t1(run_server,0);
	//std::thread t2(run_server,1);
	//std::thread t3(run_server,2);
	//std::thread t1(run_server, server_id);

	//if(server_id==0) {
		//t4=std::thread(run_client,0);
		//t5=std::thread(run_client,1);
		//t6=std::thread(run_client,2);
		//t7=std::thread(run_client,3);
		//t8=std::thread(run_client,4);
	//}

	//t1.join();
	//t2.join();
	//t3.join();
	//if(server_id==0) {
		//t4.join();
		//t5.join();
		//t6.join();
		//t7.join();
		//t8.join();
	//}

    return 0;
}
