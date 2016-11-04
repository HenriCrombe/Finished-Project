all: sender receiver

sender: sender.c utils.c
	gcc -Wall -o sender sender.c utils.c -lz

receiver: receiver.c utils.c
	gcc -Wall -o receiver receiver.c utils.c -lz
clean: 
	rm -f sender sender.o receiver.o receiver
cleansender:
	rm -f sender sender.o
cleanreceiver:
	rm -f receiver.o receiver
