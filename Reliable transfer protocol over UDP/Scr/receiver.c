#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
#include "utils.h"

/*
 * classic use:
 * ./sender --file file.txt --sber 24 --splr 2 --delay 100 hostIPaddr 5000
 *  (or host IPv4 or IPv6 addr)
 */

int main(int argc,char **argv){
	
	struct receiver_arguments *args = malloc(sizeof(struct receiver_arguments));

	int start = TRUE;
	int file_size = 0;
	int transfer_finished = FALSE;
	

	args->filename=NULL;
	get_receiver_args(argc,argv,args);
	print_receiver_args(args);


	int fd_out;
	int fd_file;
	if(!args->filename){
		fd_out = 1;
	}
	else{
		fd_file = open(args->filename,O_WRONLY | O_CREAT | O_TRUNC, 0666);
		fd_out = fd_file;
	}


	// struct sockaddr_in *client;
	int socket_fd = create_socket_fd(0,args->hostname,args->port);
	// client_addr contains IP addr of the client
	struct sockaddr_storage client_addr;
	socklen_t client_len=sizeof(client_addr);

	// main window
	int * window_status= malloc(sizeof(int)*256);
	struct frame ** window_frames = malloc(sizeof(struct frame *)*256);

	int i;
	for(i=0;i<256;++i){
		window_status[i] = FREE;
		window_frames[i] = NULL;
	}

	int expected_seq = 0;
	int window_size = 4;

	// START INFINITE LOOP	
	while(1){

		int discard = FALSE;
	
		fd_set rdset;
		FD_ZERO(&rdset);
		FD_SET(socket_fd,&rdset);	
		int max_fd = socket_fd + 1;

		// SELECT BLOCKING CALL
		int s = select(max_fd+1,&rdset,NULL,NULL,NULL);

		if(s == -1 ){
			printf("Select failed \n");
			return EXIT_FAILURE;
		}

		if(s == 0 )
			//DONT CARE IN RECEIVER
			printf("Timeout\n");

		/** SOMETHING TO READ IN SOCKET UDP? */
		if(FD_ISSET(socket_fd,&rdset)){
			if(start == TRUE){
				printf("Receiving data ...\n");
				start = FALSE;
			}
			// A FRAME HAS BEEN RECVEID!
			struct frame *f = malloc(sizeof(struct frame));
			int size = sizeof(struct frame);
			int byte_count =-1;
			// RECEIVE INCOMING FRAME
			if ((byte_count = recvfrom(socket_fd, f, size, 0, (struct sockaddr*) &client_addr, &client_len)) < 0){
				printf("Error rcv first frame\n");
				close(socket_fd);
				exit(0);
			}
			
			// DONT FORGET TO DECODE INCAME FRAMES
			decode_frame(f);

			uint32_t in_crc = get_CRC32(f);

			if ( f->crc != in_crc )
				discard = TRUE;

			// printf("FRAME RCVED : seq : %d - expected seq %d\n",f->seq,expected_seq);

			if(f->type != PTYPE_DATA){
				// drop packet TODO=CHECK CRC
				discard = TRUE;
			}

			if(discard == FALSE){
				// IF THE INCOMING FRAME IS CORRECT
				if(f->seq == expected_seq){	

					if (f->length < 512)
						transfer_finished = TRUE;
					
					write(fd_out,f->payload,f->length);
					file_size += f->length;

					window_status[f->seq] = FREE;
				
					int s = f->seq+1;
					int s_max = s+window_size;
					free(f);
					
					while(  s < 256 && s < s_max ){
						if(window_status[s] == STORED){
							write(fd_out,window_frames[s]->payload,window_frames[s]->length);
							file_size += window_frames[s]->length;

							if (window_frames[s]->length < 512){
								transfer_finished = TRUE;
							}
							window_status[s] = FREE;
							free(window_frames[s]);
							s += 1;
						}
						else
							break;
					}	

					if(s > 255){
						for(i=0;i<256;++i){
							window_status[i] = FREE;					
						}
						expected_seq = 0;	
					}

					else{
						expected_seq = s;
					}

					send_ack(socket_fd,window_size,expected_seq,client_addr,client_len);

					if(transfer_finished == TRUE){
						printf("Transfer finished ! %d byte(s) transfered \n",file_size);

						// free_receiver_args(args);
						// free_windows(window_frames,window_status);
						// free(window_frames);
						free(window_status);
						free(args);
						free(window_frames);
						close(socket_fd);
						close(fd_file);
						
						return EXIT_SUCCESS;
					}			
				}

				else if( f->seq > expected_seq &&  f->seq < f->seq+window_size && discard == FALSE){
					// CUR. FRAME HAS TO BE STORED AND ACKNOWLEDGED
					if (window_status[f->seq] == FREE ){
						// printf("STORED TEMPORARLY: seq rcved : %d expected_seq : %d\n",f->seq,expected_seq);
						window_frames[f->seq] = f;
						window_status[f->seq] = STORED;
						send_ack(socket_fd,window_size,expected_seq,client_addr,client_len);	
					}
				}
			}
			else{
				// DISCARD == TRUE
				free(f);
			}
		}
	}	
	
	return 1;
}
