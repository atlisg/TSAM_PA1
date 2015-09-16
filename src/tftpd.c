/* A UDP echo server with timeouts.
 *
 * Note that you will not need to use select and the timeout for a
 * tftp server. However, select is also useful if you want to receive
 * from multiple sockets at the same time. Read the documentation for
 * select on how to do this (Hint: Iterate with FD_ISSET()).
 */

#include <assert.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
	int sockfd;
        struct sockaddr_in server, client;
        char message[512];
	
        /* Create and bind a UDP socket */
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        memset(&server, 0, sizeof(server));
        server.sin_family = AF_INET;
        /* Network functions need arguments in network byte order instead of
           host byte order. The macros htonl, htons convert the values, */
        server.sin_addr.s_addr = htonl(INADDR_ANY);
        server.sin_port = htons(3412);
        bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

	/* TFTP Formats */

	// Read request (RRQ) / Write request (WRQ)
	typedef struct RRQ_WRQ {
        	u_short opcode; // Op #01/02 (2 bytes)
        	char filename[32]; // Filename (string)
        	u_short pad1; // Padding (1 byte)
        	char mode[32]; // Mode (string)
        	u_short pad2; // Padding (1 byte)
        } RRQ_WRQ;

	// Data (DATA)
	typedef struct DATA {
        	u_short opcode; // Op #03 (2 bytes)
        	u_short blocknr; // Block # (2 bytes)
        	char data[508]; // Data (n bytes)
        } DATA;

        // Acknowledgment (ACK)
        typedef struct ACK {
        	u_short opcode; // Op #04 (2 bytes)
        	u_short blocknr; // Block # (2 bytes)
        } ACK;

        // Error (ERROR)
        typedef struct ERROR {
        	u_short opcode; // Op #05 (2 bytes)
        	u_short errorcode; // ErrorCode (2 bytes)
        	char errmsg[32]; // ErrMsg (string)
        	u_short pad; // Padding (1 byte)
        } ERROR;
	
	char buffer[30000];
	DATA list_of_data_blocks[100];

	int k;
	for (k = 0; k < 3; k++) {
                fd_set rfds;
                struct timeval tv;
                int retval;

                /* Check whether there is data on the socket fd. */
                FD_ZERO(&rfds);
                FD_SET(sockfd, &rfds);

                /* Wait for five seconds. */
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                retval = select(sockfd + 1, &rfds, NULL, NULL, &tv);

                if (retval == -1) {
                        perror("select()");
                } else if (retval > 0) {
                        /* Data is available, receive it. */
                        assert(FD_ISSET(sockfd, &rfds));
                        
                        /* Copy to len, since recvfrom may change it. */
                        socklen_t len = (socklen_t) sizeof(client);
                        /* Receive one byte less than declared,
                           because it will be zero-termianted
                           below. */
                        ssize_t n = recvfrom(sockfd, message,
                                             sizeof(message) - 1, 0,
                                             (struct sockaddr *) &client,
                                             &len);
			
			if (message[1] == 0x1) {
				fprintf(stdout, "k: %d\n", k);
				// RRQ
				// Fill struct
				RRQ_WRQ rrq;
				rrq.opcode = message[1];
				int i;
				int namelen;
				int modelen;
				rrq.filename[0] = 'd';
				rrq.filename[1] = 'a';
				rrq.filename[2] = 't';
				rrq.filename[3] = 'a';
				rrq.filename[4] = '/';
				for (i = 2, namelen = 5; message[i] != '\0'; i++) {
					rrq.filename[i+3] = message[i];
					namelen++;
				}
				rrq.filename[namelen] = '\0';
				for (i++, modelen = 0; message[i] != '\0'; i++) {
					rrq.mode[i+5-(namelen+3)] = message[i];
					modelen++;
				}
				rrq.mode[modelen] = '\0';

				// Open the requested file and put the content in buffer
				int fd;
				fd = open(rrq.filename, O_RDONLY);
				ssize_t size;
				size = read(fd, buffer, 30000);
				
				// Chop the buffer up in pieces:
				for (i = 0; i * 508 < size; i++) {
					DATA data;
					data.opcode = 0x3;
					data.blocknr = i+1; // hér
					int j;
					for (j = 0; j < 508 && i * 508 + j < size; j++) {
						data.data[j] = buffer[i * 508 + j];
					}
					list_of_data_blocks[i] = data;
				}
				char msg[512];
				DATA m = list_of_data_blocks[0];
				msg[0] = m.opcode >> 8;
				msg[1] = m.opcode; 
				msg[2] = m.blocknr >> 8;
				msg[3] = m.blocknr; // O_o
				for (i = 0; i < 508; i++) {
					msg[i+4] = m.data[i]; // :D ERTU MEMMÉR?????
				}
				sendto(sockfd, msg, 512, 0, 
					(struct sockaddr *) &client,
					(socklen_t) sizeof(client));
			}

			if (message[1] == 0x4) {
				// ACK
				fprintf(stdout, "Acknie!\n");
				int next;
				next = (message[2] >> 8) + message[3];
				fprintf(stdout, "next: %d\n", next);
				DATA m = list_of_data_blocks[next+1];
				char msg[512];
				msg[0] = m.opcode >> 8;
				msg[1] = m.opcode; 
				msg[2] = m.blocknr >> 8;
				msg[3] = m.blocknr; // O_o
				int i;
				for (i = 0; i < 508; i++) {
					msg[i+4] = m.data[i]; // :D ERTU MEMMÉR?????
					if (i < 20) {
						fprintf(stdout, "msg[%d]: %x\n", i, msg[i]);
					}
				}
				sendto(sockfd, msg, 512, 0,
					(struct sockaddr *) &client,
					(socklen_t) sizeof(client));
			}

                        fflush(stdout);
                } else {
                        fprintf(stdout, "No message in five seconds.\n");
                        fflush(stdout);
                }
        }
}
