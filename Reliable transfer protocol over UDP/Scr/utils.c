/*
 * Utils class
 * 
 */

#include "utils.h"



int send_frame(int socket_fd, struct frame *f){
	int s = -1;
	// SERIALIZE
	
	if ((s = send(socket_fd, f, sizeof(struct frame), 0)) < 0){
		printf("Error, send_frame() status :%d",s);
		close(socket_fd);
		exit(0);
	}
	return 1;
}

int send_ack(int socket_fd, int window_index, int sequence, struct sockaddr_storage client_addr, socklen_t client_len){

	struct frame *ack_frame = malloc(sizeof(struct frame));
	char * payload = malloc(sizeof(char)*512);
	create_ack_frame(window_index, sequence, ack_frame,payload);
	prepare_frame_for_sending(ack_frame);
	sendto(socket_fd,ack_frame,sizeof(struct frame ),0,(struct sockaddr*)&client_addr,client_len);
	free(payload);
	free(ack_frame);
	return 1;

}

int create_ack_frame(uint32_t window,uint32_t sequence,struct frame *f,char * payload){

	f->type=PTYPE_ACK;
	f->window=window;
	f->seq=sequence;
	f->length=0;
	bzero(payload,512);
	memmove(f->payload,payload,512);

	// TODO MAKE CRC FOR ACK PACKET !!!!
	f->crc =  get_CRC32(f);	

	return 1;
}

int read_payload(int fd, char * payload){
	
	int nread=-1;
	if((nread=read(fd,payload,sizeof(char)*512)) < 0){
		printf("Error,  read_payload status :%d\n",nread);
		exit(0);
	}
	return nread;
}

int create_frame(uint32_t type,uint32_t window ,uint32_t  seq_number ,uint32_t len , char *payload,struct frame *f){
	
	f->type=type;
	f->window=window;
	f->seq=seq_number;
	f->length=len;
	memmove(f->payload,payload,len);
	free(payload);
	// TODO CRC ON ALL FRAME !!
	f->crc = get_CRC32(f);	
	return 1;

}

uint32_t get_CRC32(struct frame *f){
	char buff[sizeof(struct frame)-4];
	memmove(buff,f,sizeof(struct frame)-4);
	return (uint32_t) crc32(0,(const void *) buff,sizeof(struct frame)-4);
}


void  prepare_frame_for_sending(struct frame *f){
	f->length =  htons(f->length);
	f->crc = htonl(f->crc);
}


void decode_frame(struct frame *f){
	f->length =  ntohs(f->length);
	f->crc = ntohl(f->crc);
}

int create_socket_fd(int direction,char *hostname,char *port){
	
	int socket_fd;
	struct addrinfo hints, *client_addr; 

	//Init hints
	memset(&hints, 0, sizeof (struct addrinfo));
	hints.ai_family = AF_UNSPEC; // Force IPv4, IPV6 not working yet.Dunno why
	hints.ai_socktype = SOCK_DGRAM;	

	hints.ai_flags = AI_PASSIVE;
			
	// Get adress informations needed to connect
	int status;
	
	if ((status = getaddrinfo(hostname, port, &hints, &client_addr)) != 0){
		printf("Error getaddrinfo :%d",status);
		p_err("\nError, get adress info\n");
	}
	
	// Socket description initialization
	if((socket_fd = socket(client_addr->ai_family, client_addr->ai_socktype, client_addr->ai_protocol)) < 0){
		p_err("\nError, unable to create the socket !\n");
	}
		
	
	if(direction == 0){
		// if direction == 0, the socket needs to listen for datagram
		// Bind socket to client adress
		if (bind(socket_fd, client_addr->ai_addr, client_addr->ai_addrlen) < 0){
			close(socket_fd);
			p_err("\nFailed to bind socket to server adress\n");	
		}	
		// printf("Socket created (IN) : Binded to client\n");
	}

	if(direction == 1){
		// if direction == 1, the created socket needs to send datagrams
		// connect to client adress
		if((status =connect(socket_fd,client_addr->ai_addr, client_addr->ai_addrlen)) < 0 ){
			printf("Error connect() : %d\n",status);
			close(socket_fd);
			exit(0);
		}	
		// printf("Socket created (OUT): Connected\n");
	}
	freeaddrinfo(client_addr);
	return socket_fd;
}



int get_sender_args(int argc, char** argv, struct sender_arguments *args){
	
	int opt = 0;
	static struct option long_options[] = {
		{"file" , required_argument, NULL, 'f' },
        {"sber" , required_argument, NULL, 's' },
        {"splr" , required_argument, NULL, 'p' },
        {"delay", required_argument, NULL, 'd' },
        {NULL, 0, NULL, 0}
    };
      
    int long_index =0;
    
	while ((opt = getopt_long(argc, argv,"fsp:d:", 
                   long_options, &long_index )) != -1) {
					   
		switch (opt) {
             case 'f' :
             		args->filename = optarg;
                 	break;
             case 's' : 
             		args->sber=atoi(optarg);
                 	break;
             case 'p' : 
             		args->splr=atoi(optarg); 
                 	break;
             case 'd' : 
             		args->delay=atoi(optarg);
                 	break;
             default: printf("Fail to get args"); 
                 	return -1;
        }
    }
     
    args->hostname = argv[optind];
    optind += 1;
    args->port = argv[optind];
    return 1;
}

int get_receiver_args(int argc, char** argv, struct receiver_arguments *args){
	int opt = 0;
	
	static struct option long_options[] = {
		{"file",      required_argument,       0,  'f' },
        {0,           0,                 0,  0   }
    };
    
    int long_index =0;
    
	while ((opt = getopt_long(argc, argv,"f::", 
                   long_options, &long_index )) != -1) {
					   
		switch (opt) {
             case 'f' : args->filename=optarg;
                 break;
             default: printf("Fail to get args"); 
                 return -1;
        }
    }
    
    args->hostname = argv[optind];
    optind += 1;
    args->port = argv[optind];
   
	return 1;		
}


/* Perform timeval subtraction
    - "to - from = result"
    - return -1 if 'from' is larget(later) than 'to'
    - return 0 if success */
int subtract_timeval(struct timeval *to, struct timeval *from, struct timeval *result) {
    if (compare_timeval(to, from) < 0)
        return -1;
    result->tv_sec = to->tv_sec - from->tv_sec;
    result->tv_usec = to->tv_usec - from->tv_usec;
    if(result->tv_usec < 0)    {
        result->tv_sec--;
        result->tv_usec += 1000000;
    }    
    return 0;
}

int add_timeval(struct timeval *to, struct timeval *from, struct timeval *result) {

    result->tv_sec = to->tv_sec + from->tv_sec;
    result->tv_usec = to->tv_usec + from->tv_usec;
    if(result->tv_usec > 1000000 )    {
        result->tv_sec++;
        result->tv_usec -= 1000000;
    }    
    return 0;
}


int get_timeout(unsigned long delay,struct timeval *next_timeout){

	struct timeval cur_time;
	struct timeval timeout;

	gettimeofday(&timeout,NULL);	
	gettimeofday(&cur_time,NULL);


	timeout.tv_sec = 0;
	timeout.tv_usec = delay;

	add_timeval(&timeout, &cur_time, next_timeout);

	return 1;
}


int compare_timeval(struct timeval *a, struct timeval *b) {
    if (a->tv_sec > b->tv_sec)
        return 1;
    else if (a->tv_sec < b->tv_sec)
        return -1;
    else if (a->tv_usec > b->tv_usec)
        return 1;
    else if (a->tv_usec < b->tv_usec)
        return -1;
    return 0;
}


void print_sender_args(struct sender_arguments *args){
	printf("Sender arguments :\n \
	-host : %s\n \
	-file : %s\n \
	-sber : %d\n \
	-splr : %d\n \
	-delay: %d\n \
	-port : %s\n", args->hostname,args->filename,args->sber,args->splr,args->delay,args->port);
}

void print_receiver_args(struct receiver_arguments *args){
	printf("Receiver arguments :\n \
	-host : %s\n \
	-file : %s\n \
	-port : %s\n", args->hostname,args->filename,args->port);
}

void * get_in_addr(struct sockaddr *sa){
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

char *remove_newline(char *s){
    int len = strlen(s);

    if (len > 0 && s[len-1] == '\n')  // if there's a newline
        s[len-1] = '\0';          // truncate the string

    return s;
}
void print_frame(struct frame *f){
	printf("Frame content :\n \
	-type : %d\n \
	-window : %d\n \
	-seq : %d\n \
	-length : %d\n \
	-payload: \n%s\n \
	-CRC: %d\n ", f->type,f->window,f->seq,f->length,f->payload,f->crc);
}
void print_frame1(struct frame *f){
	printf("Frame content :\n \
	-type : %d\n \
	-window : %d\n \
	-seq : %d\n \
	-length : %d\n \
	-payload: \n\n \
	-CRC: %d\n ", f->type,f->window,f->seq,f->length,f->crc);
}

void free_sender_args(struct sender_arguments* args){
	if (args->filename != NULL){
		free(args->filename);
	}
	if (args->hostname != NULL){
		free(args->hostname);	
	}
	if (args->port != NULL){
		free(args->port);	
	}
	free(args);
}

void free_receiver_args(struct receiver_arguments* args){
	if (args->filename != NULL){
		free(args->filename);
	}
	if (args->hostname != NULL){
		free(args->hostname);	
	}
	if (args->port != NULL){
		free(args->port);	
	}
	free(args);
}

void free_windows(struct frame** wf, int * window_status){
	int i;
	for (i=0; i<256; ++i){
		if (wf[i] != NULL){
			free(wf[i]);
		}
	}
	free(wf);
	free(window_status);
}

int p_err(char * str){
	perror(str);
	exit(0);
}
