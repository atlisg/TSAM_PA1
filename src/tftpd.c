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

	for (;;) {
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

			if (message[1] == 00000001) {
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
				fprintf(stdout, "rrq.filename: %s\n", rrq.filename);
				fprintf(stdout, "rrq.mode: %s\n", rrq.mode);

				// open the requested file and chop in up
				// in appropriately sized packets
				int fd;
				fd = open(rrq.filename, O_RDONLY);
				char buffer[30000];
				if (read(fd, buffer, 30000) > 0) {
					for (i = 0; i < 30000; i++) {
						fprintf(stdout, "%c", buffer[i]);
					}
					close(fd);
				} else {
					fprintf(stdout, "READ ERROR\n");
				}
			}

			/* Send the message back. */
                        sendto(sockfd, message, (size_t) n, 0,
                               (struct sockaddr *) &client,
                               (socklen_t) sizeof(client));
                        /* Zero terminate the message, otherwise
                           printf may access memory outside of the
                           string. */
                        message[n] = '\0';
                        /* Print the message to stdout and flush. */
                        fprintf(stdout, "Received:\n%s\n", message);
                        fflush(stdout);
                } else {
                        fprintf(stdout, "No message in five seconds.\n");
                        fflush(stdout);
                }
        }
}
