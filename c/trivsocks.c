/* Copyright 2004-2007 Roger Dingledine, Nick Mathewson */
/* Copyright 2007 Roger Dingledine, Nick Mathewson, Steven J. Murdoch */
/* See LICENSE for licensing information */

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>

#define RESPONSE_LEN_4 8
#define RESPONSE_LEN_5 4
#define HTTP_BUF_LEN 256
#define HTTP_READ_LEN 16 // Must be <= (HTTP_BUF_LEN - 1)

// Timestamps of important events
struct timeval starttime; // Connection process started
struct timeval sockettime; // After socket is created
struct timeval connecttime; // After socket is connected
struct timeval negotiatetime; // After authentication methods are negotiated (SOCKS 5 only)
struct timeval requesttime; // After SOCKS request is sent
struct timeval responsetime; // After SOCKS response is received
struct timeval datarequesttime; // After HTTP request is written
struct timeval dataresponsetime; // After first response is received
struct timeval datacompletetime; // After payload is complete
// After (i + 1) * 10% of expected bytes are received
struct timeval dataperctime[9];

// Data counters of SOCKS payload
size_t read_bytes;
size_t write_bytes;

// Did we get killed by the bash 'timeout' widget?
int didtimeout;

static void usage(void) __attribute__((noreturn));


/** Set *<b>out</b> to a newly allocated SOCKS4a resolve request with
 * <b>username</b> and <b>hostname</b> as provided.  Return the number
 * of bytes in the request. */
static int build_socks_connect_request(char **out, const char *username, 
				       const char *hostname, int reverse,
                            	       int version) {
  	size_t len = 0;
  
  	if (version == 4) {
    		len = 8 + strlen(username) + 1 + strlen(hostname) + 1;
    		*out = malloc(len);
    		(*out)[0] = 4;      /* SOCKS version 4 */
    		(*out)[1] = '\x01'; /* Command: connect. */
    		set_uint16((*out)+2, htons(80)); /* port: 80. */
    		set_uint32((*out)+4, htonl(0x00000001u)); /* addr: 0.0.0.1 */
    		memcpy((*out)+8, username, strlen(username)+1);
    		memcpy((*out)+8+strlen(username)+1, hostname, strlen(hostname)+1);
  	} else if (version == 5) {
    		int is_ip_address;
    		struct in_addr in;
    		size_t addrlen;
    		is_ip_address = inet_aton(hostname, &in);
    
		if (!is_ip_address && reverse) {
      			fprintf(stderr,"Tried to do a reverse lookup on a non-IP!\n");
      			return -1;
    		}
    
		addrlen = is_ip_address ? 4 : 1 + strlen(hostname);
    		len = 6 + addrlen;
    		*out = malloc(len);
    		(*out)[0] = 5; /* SOCKS version 5 */
    		(*out)[1] = '\x01'; /* connect. */
    		(*out)[2] = 0; /* reserved. */
    		(*out)[3] = is_ip_address ? 1 : 3;
    
		if (is_ip_address) {
      			set_uint32((*out)+4, in.s_addr);
    		} else {
      			(*out)[4] = (char)(uint8_t)(addrlen - 1);
      			memcpy((*out)+5, hostname, addrlen - 1);
    		}
    
		set_uint16((*out)+4+addrlen, htons(80)); /* port */

  	} else exit(0);

  	return len;
}

/** Given a <b>len</b>-byte SOCKS4a response in <b>response</b>, set
 * *<b>addr_out</b> to the address it contains (in host order).
 * Return 0 on success, -1 on error.
 */
static int parse_socks4a_connect_response(const char *response, size_t len,
                               uint32_t *addr_out) {
  	uint8_t status = 0;
  	
  	if (len < RESPONSE_LEN_4) {
    		fprintf(stderr, "Truncated socks response.\n");
    		return -1;
  	}

  	if (((uint8_t)response[0]) != 0) { /* version: 0 */
    		fprintf(stderr, "Nonzero version in socks response: bad format.\n");
    		return -1;
  	}

  	status = (uint8_t)response[1];
  	
	if (get_uint16(response+2) != 0) { /* port: 0 */
    		fprintf(stderr, "Nonzero port in socks response: bad format.\n");
    		return -1;
  	}
  
	fprintf(stderr, "Port number: %u\n", (unsigned)get_uint16(response+2));
  
	if (status != 90) {
    		fprintf(stderr, 
			"Got status response '%u': socks request failed.\n", 
			(unsigned)status);
    		
		return -1;
  	}

  	*addr_out = ntohl(get_uint32(response+4));
  	
	return 0;
}

static int parse_socks5_connect_response(const char *response, size_t len, int s,
                              uint32_t *result_addr, char **result_hostname) {
  	char reply_buf[4];
  	uint16_t port;

  	if (len < RESPONSE_LEN_5) {
    		fprintf(stderr, "Truncated socks response.\n");
    		return -1;
  	}
  
	if (response[0] != 5) {
    		fprintf(stderr, "Bad SOCKS5 reply version\n");
    		return -1;
  	}
  
	if (response[1] != 0) {
    		fprintf(stderr, "Got status response '%u': SOCKS5 request failed\n",
            	(unsigned)response[1]);
    		return -1;
  	}

  	if (response[3] == 1) {
    		/* IPv4 address */
    		if (read_all(s, reply_buf, 4, 1) != 4) {
      			fprintf(stderr,"Error reading address in socks5 response\n");
      			return -1;
    		}
    
		*result_addr = ntohl(get_uint32(reply_buf));
  	} else if (response[3] == 3) {
    		size_t result_len;
    
		if (read_all(s, reply_buf, 1, 1) != 1) {
      			fprintf(stderr,"Error reading address_length in socks5 response\n");
      			return -1;
    		}
    
		result_len = *(uint8_t*)(reply_buf);
    		*result_hostname = malloc(result_len+1);
    		
		if (read_all(s, *result_hostname, result_len, 1) != 
		    (int) result_len) {
      			fprintf(stderr,"Error reading hostname in socks5 response\n");
      			return -1;
    		}
    
		(*result_hostname)[result_len] = '\0';
  	}

  	if (read_all(s, reply_buf, 2, 1) != 2) {
    		fprintf(stderr,"Error reading port in socks5 response\n");
    		return -1;
  	}
  
	port = ntohl(get_uint16(reply_buf));
  	fprintf(stderr,"Port number: %u\n", (unsigned)port);
  	return 0;
}

static int do_socks5_negotiate(int s) {
	char method_buf[2];
    	
	if (write_all(s, "\x05\x01\x00", 3, 1) != 3) {
      		perror("sending SOCKS5 method list");
      		return -1;
    	}
    
	if (read_all(s, method_buf, 2, 1) != 2) {
      		perror("reading SOCKS5 methods");
      		return -1;
    	}
    
	if (method_buf[0] != '\x05') {
      		perror("unrecognized SOCKS version");
      		return -1;
    	}

    	if (method_buf[1] != '\x00') {
      		perror("unrecognized socks authentication method");
      		return -1;
    	}
    
	return 0;
}

int do_http_get(int s, const char *path, const char *hostname,
            size_t expected_bytes, size_t *read_bytes, size_t *write_bytes,
            struct timeval *datarequesttime, struct timeval *dataresponsetime,
            struct timeval *datacompletetime, struct timeval *dataperctime) {

	char buf[HTTP_BUF_LEN];
  	int len; // Length of request, not including \0
  	int perc_logged = -1; // Last logged timestamp for fraction of received bytes
  	char is_first = 1;

  	len = snprintf(buf, HTTP_BUF_LEN, "GET %s HTTP/1.0\r\nPragma: no-cache\r\n"
                                    "Host: %s\r\n\r\n", path, hostname);

  	// Check for overflow or error
  	if (len >= HTTP_BUF_LEN || len < 0) return -1;

  	// Write the request
  	fprintf(stderr, "Response: %s\n", buf);
  
	if (write_all(s, buf, len, 1) != len) return -1;
  
	*write_bytes = len;
  
	// Get when request is sent
  	if (gettimeofday(datarequesttime, NULL)) {
    		perror("getting datarequesttime");
    		return -1;
  	}	    

  	// Half-close the socket
  	//if (shutdown(s, SHUT_WR))
  	//	return -1;

  	// Default, in case no data is returned
  	dataresponsetime -> tv_sec = dataresponsetime -> tv_usec = 0;

  	// Read the response
  	*read_bytes = 0;
  
	while ((len = read_all(s, buf, HTTP_READ_LEN, 1)) > 0) {
    		buf[len] = '\0';
    		fprintf(stderr, "Response: %s\n", buf);
    		*read_bytes += len;
    
		// Get when start of response was received
    		if (is_first) {
      			is_first = 0;
      			
			if (gettimeofday(dataresponsetime, NULL)) {
        			perror("getting dataresponsetime");
        			return -1;
      			}
    		}

    		// Get when the next 10% of expected bytes are received; this is a
    		// while loop for cases when we expect only very few bytes and read
    		// more than 10% of them in a single read_all() call.
    		while (*read_bytes < expected_bytes &&
               	       (*read_bytes * 10) / expected_bytes > perc_logged + 1) {
      		
			if (gettimeofday(&dataperctime[++perc_logged], NULL)) {
       				perror("getting dataperctime");
       				return -1;
      			}
    		}
  	}

  	// Get when response is complete
  	if (gettimeofday(datacompletetime, NULL)) {
    		perror("getting datacompletetime");
    		return -1;
  	}
  
  	return len;
}

static int print_time(struct timeval t) {
 	return printf("%ld %ld ", (long int)t.tv_sec, (long int)t.tv_usec);
}

static void output_status_information(void) {
  	int i;

  	print_time(starttime);
  	print_time(sockettime);
  	print_time(connecttime);
  	print_time(negotiatetime);
  	print_time(requesttime);
  	print_time(responsetime);
  	print_time(datarequesttime);
  	print_time(dataresponsetime);
  	print_time(datacompletetime);

  	printf("%lu %lu ", (unsigned long)write_bytes, (unsigned long)read_bytes);
  	printf("%d ", didtimeout);

  	for (i = 0; i < 9; i++) print_time(dataperctime[i]);
  
	printf("\n");
}

/** Send a resolve request for <b>hostname</b> to the Tor listening on
 * <b>sockshost</b>:<b>socksport</b>.  Store the resulting IPv4
 * address (in host order) into *<b>result_addr</b>.
 */
static int do_connect(const char *hostname, const char *filename, 
		      uint32_t sockshost, uint16_t socksport,
           	      int reverse, int version, size_t expected_bytes,
           	      uint32_t *result_addr, char **result_hostname) {

  	char *req = NULL;
  	int s;
  	int len = 0;
  	int retval;
  	struct sockaddr_in socksaddr;

  	*result_addr = 0;
  	*result_hostname = NULL;

  	// Get time that connection was started
  	if (gettimeofday(&starttime, NULL)) {
    		perror("getting starttime");
    		return -1;
  	}

  	// Create the socket for connecting to SOCKS server
  	s = socket(PF_INET,SOCK_STREAM,IPPROTO_TCP);
  
	if (s < 0) {
    		perror("creating socket");
    		return -1;
  	}

  	// Get time that socket was created
  	if (gettimeofday(&sockettime, NULL)) {
    		perror("getting sockettime");
    		return -1;
  	}

  	// Connect to the SOCKS server
  	memset(&socksaddr, 0, sizeof(socksaddr));
  	socksaddr.sin_family = AF_INET;
  	socksaddr.sin_port = htons(socksport);
  	socksaddr.sin_addr.s_addr = htonl(sockshost);
  	
	if (connect(s, (struct sockaddr*)&socksaddr, sizeof(socksaddr))) {
    		perror("connecting to SOCKS host");
    		return -1;
  	}
  
	// Get time that socket was connected
  	if (gettimeofday(&connecttime, NULL)) {
    		perror("getting connecttime");
    		return -1;
  	}

  	// Negotiate authentication method for SOCKS 5
  	if (version == 5) {
    		retval = do_socks5_negotiate(s);
    		
		if (retval) return retval;
  	}  
  
	// Get time that negotiation was completed
  	if (gettimeofday(&negotiatetime, NULL)) {
    		perror("getting negotiatetime");
    		return -1;
  	}

  	if ((len = 
	     build_socks_connect_request(&req, "", hostname, reverse, 
					 version)) < 0) {
    		fprintf(stderr, "error generating SOCKS request: %d\n", len);
    		return -1;
  	}

  	if (write_all(s, req, len, 1) != len) {
    		perror("sending SOCKS request");
    		free(req);
    		return -1;
  	}
  
	free(req);
  
	// Get time that request was sent
  	if (gettimeofday(&requesttime, NULL)) {
    		perror("getting requesttime");
    		return -1;
  	}

  	if (version == 4) {
    		char reply_buf[RESPONSE_LEN_4];
    
		if (read_all(s, reply_buf, RESPONSE_LEN_4, 1) != RESPONSE_LEN_4) {
      			fprintf(stderr, "Error reading SOCKS4 response.\n");
      			return -1;
    		}
    
		if (parse_socks4a_connect_response(reply_buf, RESPONSE_LEN_4,
                                       result_addr) < 0){
      			return -1;
    		}

  	} else {
    		char reply_buf[RESPONSE_LEN_5];
    
		if (read_all(s, reply_buf, RESPONSE_LEN_5, 1) != RESPONSE_LEN_5) {
      			fprintf(stderr, "Error reading SOCKS5 response\n");
      			return -1;
    		}
    
		if (parse_socks5_connect_response(reply_buf, RESPONSE_LEN_5, s,
                                      result_addr, result_hostname) < 0)
      			return -1;
  	}

  	// Get time that response was received
  	if (gettimeofday(&responsetime, NULL)) {
    		perror("getting responsetime");
    		return -1;
  	}

  /*
  char reply_buf[1];
  while (read_all(s, reply_buf, 1, 1) != 0) {
    fprintf(stderr,"Extra data: 0x%x\n", reply_buf[0]);
  } 
  */
  
  	// Request a file using HTTP
  	do_http_get(s, filename, hostname, expected_bytes, &read_bytes, &write_bytes,
             	    &datarequesttime, &dataresponsetime, &datacompletetime,
             	    dataperctime);


  	didtimeout = 0;

  	// Output status information
  	output_status_information();

  	return 0;
}

static void usage() {
  	puts("Usage: ./trivsocks [ -4 | -5 | -x ] address:port path-to-file exp-bytes");
  	exit(0);
}

static void termination_handler(int signum) {
	fprintf(stderr,"Received a timeout. Exiting.\n");
	didtimeout = 1;

  	// Get when response is complete
  	if (gettimeofday(&datacompletetime, NULL)) {
    		perror("getting datacompletetime for timeout");
  	}

  	output_status_information();

  	exit(0);
}

/** Entry point to tor-resolve */
int main(int argc, char **argv) {
  	char *result_hostname = NULL;
  	char *hostname = NULL;
	char *filename = NULL;
	int isSocks4 = 0;
	int isReverse = 0;
  	size_t expbytes = 0;
	uint16_t socksport = 0;
	uint32_t sockshost;
  	uint32_t result = 0;

  	signal(SIGINT, termination_handler);

  	if (argc == 1) usage();
 
	if (!strcmp("-4", argv[1]))
      		isSocks4 = 1;
    	else if (!strcmp("-5", argv[1]))
      		isSocks4 = 0;
    	else if (!strcmp("-x", argv[1]))
      		isReverse = 1;
    	else {
      		fprintf(stderr, "Unrecognized flag '%s'\n", argv[1]);
      		usage();
    	}

  	if (isSocks4 && isReverse) {
    		fprintf(stderr, "Reverse lookups not supported with SOCKS4a\n");
    		usage();
  	}

    	if (parse_addr_port(0, argv[2], NULL, &sockshost, &socksport) < 0) {
      		fprintf(stderr, "Couldn't parse/resolve %s\n", argv[2]);
      		return 1;
    	}

    	if (!socksport) {
      		fprintf(stderr,"defaulting to port 9050\n");
      		socksport = 9050;
    	}

    	hostname = "localhost";
    	filename = strdup(argv[3]);

    	expbytes = (size_t) parse_long(argv[3], 10, 0, 1024*1024*1024, NULL, NULL);

  	if (do_connect(hostname, filename, sockshost, socksport, isReverse, 
		       isSocks4 ? 4 : 5, expbytes, &result, &result_hostname))
		return 1;

  	return 0;
}

