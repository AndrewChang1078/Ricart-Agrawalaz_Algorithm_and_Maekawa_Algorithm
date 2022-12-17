How to compile & execute?
1. Copy all files to a folder
2. make clean
3. make
4. Connect to servers on the specified machine as the table below.
5. Execute ./D on each machine.
6. Input the server type (Server:0, Client:1) and the server id for each machine.
7. Wait for about 1 minutes. (When a client machine completes a write operation, it will print logs on the terminal.)

File paths:
1. Server hosted files are under s0/, s1/, and s2/
2. Logs of servers and clients are under logs/


MAPPING_TABLE
servers:
s0
dc08
10.176.69.39

s1
dc09
10.176.69.40

s2
dc10
10.176.69.41

clients:
c0
dc11
10.176.69.42

c1
dc12
10.176.69.43

c2
dc13
10.176.69.44

c3
dc14
10.176.69.45

c4
dc15
10.176.69.46