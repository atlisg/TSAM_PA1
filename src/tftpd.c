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

/* TFTP Formats */

// Read request (RRQ) / Write request (WRQ)
typedef struct RRQ_WRQ {
       	u_short opcode; // Op #01/02 (2 bytes)
       	char filename[32]; // Filename (string)
       	u_char pad1; // Padding (1 byte)
       	char mode[32]; // Mode (string)
       	u_char pad2; // Padding (1 byte)
} RRQ_WRQ;

// Data (DATA)
typedef struct DATA {
       	u_short opcode; // Op #03 (2 bytes)
       	u_short blocknr; // Block # (2 bytes)
       	char data[512]; // Data (n bytes)
	u_short datasize; // number of bytes
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
        u_char pad; // Padding (1 byte)
} ERROR;

char *assemble_msg(DATA m)
{
	//char msg[516];
	char *msg = malloc(516);
	u_short i;
	msg[0] = m.opcode >> 8;
	msg[1] = m.opcode; 
	msg[2] = m.blocknr >> 8;
	msg[3] = m.blocknr;
	for (i = 0; i < m.datasize; i++) {
		msg[i+4] = m.data[i];
	}
	return (char *)msg;
}

ssize_t read_file(FILE* fp, RRQ_WRQ rrq, char *buffer) 
{
	// Read from file and put the content in buffer
	ssize_t size;
	size = fread(buffer, 1, 303508, fp);
	fprintf(stdout, "size: %d\n", size);
	return size;
}

DATA *chop_it(char *buffer, ssize_t size)
{
	// Chop the buffer up in pieces:
	u_short i, j;
	DATA data;
	DATA data_blocks[586];
	for (i = 0; i * 512 < size; i++) {
		data.opcode = 0x3;
		data.blocknr = i+1;
		for (j = 0; j < 512 && i * 512 + j < size; j++) {
			data.data[j] = buffer[i * 512 + j];
		}
		data.datasize = j;
		data_blocks[i] = data;
	}
	fprintf(stdout, "i: %d\n", i);
	return data_blocks;
}
DATA *list_of_data_blocks;

int main(int argc, char **argv)
{
	int sockfd;
        struct sockaddr_in server, client;
        char message[512];
	short port;
	char dir[30];
	char buffer[303508];
	FILE* fp;
	RRQ_WRQ rrq;
				
	/* Put arguments in variables and change working directory */
	sscanf(argv[1], "%d", &port);
        sscanf(argv[2], "%s", &dir);
	chdir(dir);

        /* Create and bind a UDP socket */
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        memset(&server, 0, sizeof(server));
        server.sin_family = AF_INET;
        /* Network functions need arguments in network byte order instead of
           host byte order. The macros htonl, htons convert the values, */
        server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(port);
        bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));
		
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
			
			if (message[1] == 0x1) {
				// RRQ
				// Fill struct
				rrq.opcode = message[1];
				u_short i, j;
				for (i = 0, j = 2; message[j] != '\0'; i++, j++) {
					rrq.filename[i] = message[j];
				}
				rrq.filename[i] = '\0';
				for (i = 0, j++; message[j] != '\0'; i++, j++) {
					rrq.mode[i] = message[j];
				}
				rrq.mode[i] = '\0';

				// Output of the server:
				fprintf(stdout, "file \"%s\" requested from 127.0.0.1:%d\n", 
									rrq.filename, port);
				fp = fopen(rrq.filename, "rb");
				ssize_t bytes_read = read_file(fp, rrq, buffer);
				list_of_data_blocks = chop_it(buffer, bytes_read);
				
				// Send first DATA pack:
				DATA m = list_of_data_blocks[0];
				char *msg = assemble_msg(m);
				sendto(sockfd, msg,
					(size_t) m.datasize+4, 0, 
					(struct sockaddr *) &client,
					(socklen_t) sizeof(client));
			}

			if (message[1] == 0x4) {
				// ACK
				// Fill sruct
				ACK ack;
				ack.opcode = message[1];
				u_char a, b;
				a = message[2];
				b = message[3];
				ack.blocknr = (a << 8) | b;
				// if buffer finished then chop
				if (ack.blocknr % 586 == 0) {
					ssize_t bytes_read = read_file(fp, rrq, buffer);
					list_of_data_blocks = chop_it(buffer, bytes_read);
				}
				DATA m = list_of_data_blocks[ack.blocknr % 586];
				char *msg = assemble_msg(m);

				// Send next block
				sendto(sockfd, msg,
					(size_t) m.datasize+4, 0,
					(struct sockaddr *) &client,
					(socklen_t) sizeof(client));
			}

			if (message[1] == 0x5) {
				// ERROR
				fprintf(stdout, "ERROR");
			}

                        fflush(stdout);
                } else {
                        fprintf(stdout, "No message in five seconds.\n");
                        fflush(stdout);
                }
        }
}
