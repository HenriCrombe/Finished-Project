#ifndef __UTILS_H__
#define __UTILS_H__

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <zlib.h>

#define TRUE 1
#define FALSE 0
#define PTYPE_DATA 1
#define PTYPE_ACK  2
#define WINDOW_SIZE 4
#define FREE 0
#define ACK_WAIT 1
#define ACK_RCVED 2
#define STORED 3
#define MAX_SEQ 255
#define ACK_WAIT_LAST -1

struct frame {
	uint32_t window : 5;
	uint32_t type : 3;
	uint32_t seq : 8;
	uint32_t length : 16;
	char payload[512];
	uint32_t crc;	
}__attribute__((packed));

struct sender_arguments{
	int sber;
	int splr;
	int delay;
	char *filename;
	char *hostname;
	char *port;

};

struct receiver_arguments{
	char *filename;
	char *hostname;
	char *port;
};

int send_frame(int socket_fd, struct frame *f);

int send_ack(int socket_fd, int window_index, int sequence, struct sockaddr_storage client_addr, socklen_t client_len);

int create_ack_frame(uint32_t window,uint32_t sequence,struct frame *f,char * payload);

int create_frame(uint32_t type,uint32_t window ,uint32_t  seq_number ,uint32_t len , char *payload,struct frame *f);

int create_socket_fd(int direction,char *hostname,char *port);

void decode_frame(struct frame *f);

uint32_t get_CRC32(struct frame *f);

int read_payload(int fd, char * payload);

void prepare_frame_for_sending(struct frame *f);

int get_sender_args(int argc, char** argv, struct sender_arguments *args);

int get_receiver_args(int argc, char** argv, struct receiver_arguments *args);

void print_sender_args(struct sender_arguments *args);

void print_receiver_args(struct receiver_arguments *args);

void print_frame(struct frame *f);

void print_frame1(struct frame *f);

void *get_in_addr(struct sockaddr *sa);

int p_err(char * str);

int subtract_timeval(struct timeval *to, struct timeval *from, struct timeval *result);

int compare_timeval(struct timeval *a, struct timeval *b);

uint32_t get_CRC32(struct frame *f);

int get_timeout(unsigned long delay,struct timeval *next_timeout);

char *remove_newline(char *s);

void free_sender_args(struct sender_arguments* args);

void free_receiver_args(struct receiver_arguments* args);

void free_windows(struct frame** wf, int * window_status);

#endif
