#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
#include "utils.h"
#include <math.h>
#include <time.h>



/*#include <fcntl.h>
 * classic use:
 * ./sender --file file.txt --sber 24 --splr 2 --delay 100 ::1 5000
 */

int send_file(int socket_fd, int file_fd, int file_size,struct sender_arguments *args){

	// printf("\n *** SEND FILE *** \n");

	int is_all_frame_sent = FALSE;
	int expected_seq = 0;

	unsigned long delay = args->delay;
	//Convert delay in microseconds.
	delay = (delay * 1000);
	srand (time (NULL));

	// main window status : an element of the window can be FREE or waiting for ACK (ACK_WAIT)
	int * window_status= malloc(sizeof(int)*256);

	// window_frames contains sent frames, in case of resending.
	struct frame **window_frames = malloc(sizeof(struct frame *)*256);

	// window_timeout_list contains the sent time of each frames.
	// It is updated when a frame is (re)sent.
	// With this buffer, we know the sent time of each frames in the window !
	struct timeval *window_timeout_list = malloc(sizeof(struct timeval)*256);

	int i;
	for(i=0;i<256;++i){
		window_status[i] = FREE;
	}

	// SEND FIRST  FRAME
	char *payload  = malloc(sizeof(char)*512);
	//bzero(payload,sizeof(char)*512);

	int payload_size = read_payload(file_fd,payload);


	if(payload_size >0){

		struct frame *f = malloc(sizeof(struct frame));
		create_frame(1,0,0,payload_size,payload,f);

		prepare_frame_for_sending(f);
		// printf("Sending first frame \n");
		send_frame(socket_fd,f);

		// Change status for window status @ index i (in windows_status)
		// Save frame sent in window_frames @ index i (in window_frames)
		if(payload_size<512){
			// Last packed to send
			is_all_frame_sent = TRUE;
			window_status[0] = ACK_WAIT_LAST;
		}			
		else
			window_status[0] = ACK_WAIT;

		struct timeval timeout;
		gettimeofday(&timeout,NULL);	
		get_timeout(delay,&timeout);
		// Save the frame and its timeout
		window_frames[0] = f;
		window_timeout_list[0] = timeout;	
	}
	//********************
	// START INFINITE LOOP
	//********************
	int first_time = TRUE;
	while(1){

		int altered = FALSE;
		// Set descriptor set for select()
		fd_set rdset;
		FD_ZERO(&rdset);
		FD_SET(socket_fd,&rdset);
		int max_fd = socket_fd + 1;

		// HANDLE TIMEOUT
		// Get the timeout of the last not-ack frame and compute its timeout.
		struct timeval selectTimeout;
		gettimeofday(&selectTimeout,NULL);

		struct timeval actual_timeout;
		actual_timeout.tv_usec = 0;
		actual_timeout.tv_sec = 0;
		subtract_timeval(&window_timeout_list[expected_seq],&selectTimeout,&actual_timeout);

		// BLOCKING CALL ON SELECT :
		// IF TIMEOUT OCCURS (s==0) RESEND LAST FRAME SENT 
		// IF AN ACK HAS BEEN RECEIVED, PROCESS IT.
		int s = select(max_fd+1,&rdset,NULL,NULL,&actual_timeout);

		if(s == -1 )
			p_err("Select failed\n");
		if(s == 0 ){
			// printf("Timeout occured\n");
			// IF TIMEOUT, RESEND LAST FRAME SENT NOT-ACK

			char char_save = window_frames[expected_seq]->payload[0];
			// FAKE ALTERATION
			if (random()%1000 < args->sber) {
			  	window_frames[expected_seq]->payload[0] = 0;
			  	altered = TRUE;
			}

			// FAKE SEND FAIL 
			if (random()%100 > args->splr) {			
				// printf("current seq send in resend : %d : cur expected_seq : %d \n",window_frames[expected_seq]->seq ,expected_seq);
    			// print_frame(window_frames[expected_seq]);
    			send_frame(socket_fd,window_frames[expected_seq]);	
			}
		
			if(altered == TRUE){
				window_frames[expected_seq]->payload[0] = char_save;
				altered = FALSE;
			}
    		// Update timeout
    		struct timeval timeout;
			gettimeofday(&timeout,NULL);	
			get_timeout(delay,&timeout);
			window_timeout_list[expected_seq] = timeout;
	
		}

		/** SOMETHING TO READ IN SOCKET UDP ?? */
		if(FD_ISSET(socket_fd,&rdset)){
			
			/** AN ACK HAS BEEN RECEIVED !*/
			struct sockaddr_storage host;
			socklen_t client_len=sizeof(host);
			// Alloc some memory for incoming ack_frame
			struct frame *ack_f = malloc(sizeof(struct frame));
			int size = sizeof(struct frame);

			int byte_count =-1;
			if ((byte_count = recvfrom(socket_fd, ack_f, size, 0, (struct sockaddr*)&host, &client_len)) < 0){
				printf("Cannot reach host !\n Disconnecting...\n Make sure the host is up and try again\n");
				close(socket_fd);
				exit(0);
			}

			decode_frame(ack_f);
			uint32_t in_crc = get_CRC32(ack_f);
			// printf("ACK RECEIVE : exp seq : %d win size : %d \n",ack_f->seq,ack_f->window);
			// CHECK CRC OF THE ACK_FRAME
			if ( ack_f->crc == in_crc && ack_f->type == PTYPE_ACK ){

				expected_seq = ack_f->seq;
				int max_seq = ack_f->window + ack_f->seq;
				
				// Ack for the last frame has been received, transfert is finished
				if(window_status[ack_f->seq-1] == ACK_WAIT_LAST){
					for(i=0;i<256;++i){
						if(window_frames[i] && window_status[i] != FREE)
						 	free(window_frames[i]);
					}
					free(window_frames);
					free(window_status);
					free(ack_f);
					free(window_timeout_list);

					return EXIT_SUCCESS;
				}
				// Reinitialize when seq > 255. Start new loop
				if(ack_f->seq == 0 && first_time == TRUE){
					//Ack received for last sequence
					for(i=0;i<256;++i){
						window_status[i] = FREE;
						free(window_frames[i]);
					}
					first_time = FALSE;
				}
				if(ack_f->seq > 0)
					first_time=TRUE;

				int cur_seq;
				for(cur_seq=expected_seq;cur_seq<max_seq; cur_seq++){
					
					if(cur_seq < 256 && window_status[cur_seq] == FREE && is_all_frame_sent == FALSE){
						char *payload  = malloc(sizeof(char)*512);		
						int payload_size = read_payload(file_fd,payload);

						if(payload_size > 0){
							// Initialize f with the payload read in the file
							struct frame *f = malloc(sizeof(struct frame ));
							create_frame(1,0,cur_seq,payload_size,payload,f);					
							prepare_frame_for_sending(f);

							char save = f->payload[0];
							// FAKE ALTERATION OF FRAMES
							if (random()%1000 < args->sber) {
							  	f->payload[0] = 0;
							  	altered = TRUE;
							}
							// FAKE FAIL SEND 
							if (random()%100 > args->splr) {
								send_frame(socket_fd,f);	
							}

							// Change status for window status @ index i (in windows_status)
							// Save frame sent in window_frames @ index i (in window_frames)
							if(payload_size<512){
								// Last packed to send
								is_all_frame_sent = TRUE;
								window_status[cur_seq] = ACK_WAIT_LAST;
							}
							else
								window_status[cur_seq] = ACK_WAIT;
							
							if(altered == TRUE)
								f->payload[0] = save;

							struct timeval timeout;
							gettimeofday(&timeout,NULL);	
							get_timeout(delay,&timeout);

							// Save actual frame into windows frames and save its timeout
							window_frames[cur_seq] = f;
							window_timeout_list[cur_seq] = timeout;				
						}
					}
				}		
			}
			free(ack_f);
		}
	}
	return 1;


}


int main(int argc,char **argv){
	
	struct sender_arguments *args = malloc(sizeof(struct sender_arguments));

	get_sender_args(argc,argv,args);
	print_sender_args(args);
	
	char msg[100];
	int file_fd = -1;

	int file_size=-1;
	if(!args->filename){
		// Ask for the user to write something
		printf("Write something to send (max 100 char)\n");

		fgets(msg,sizeof(msg),stdin);
		remove_newline(msg);
		file_fd = open("tempfile.txt", O_RDWR | O_CREAT | O_TRUNC ,0666);
		write(file_fd,msg,strlen(msg));
		write(file_fd,"\n",1);
		lseek( file_fd, 0, SEEK_SET );
		// printf("File sending : %d bytes\n",file_size);
		file_size = strlen(msg);
	}
	else{
		/** File opening **/
		file_fd = open(args->filename, O_RDONLY);
		struct stat s;
		lstat(args->filename, &s);
		file_size = s.st_size;
	}

	/** Connecting to host **/
	int socket_fd = create_socket_fd(1,args->hostname,args->port);

	printf("Transfert has started !\n");
	// SEND FILE
	send_file(socket_fd,file_fd,file_size,args);
		

	if(!args->filename)
		remove("tempfile.txt");

	free(args);
	close(file_fd);
	close(socket_fd);

	printf("Transfer finished : %d byte(s) transfered \n",file_size);
	//void free_sender_args(struct sender_arguments* args);	
	
	return 1;
}
		