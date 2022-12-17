/*
 * CS6378 HW2
 * Chih-An Chang
 * CXC210017
 * Ricart-Agrawala Algorithm Implementation
 */

#include "ricart.h"

int main() {
	std::string tmp;

	tmp="Starting servers...";
    std::cout << tmp << std::endl;

    /* Starting servers (0~2) */
    std::thread t1(run_server,0);
	std::thread t2(run_server,1);
	std::thread t3(run_server,2);

	std::thread t4(run_client,0);
	std::thread t5(run_client,1);
	std::thread t6(run_client,2);
	std::thread t7(run_client,3);
	std::thread t8(run_client,4);

	t1.join();
	t2.join();
	t3.join();
	t4.join();
	t5.join();
	t6.join();
	t7.join();
	t8.join();

    return 0;
}
