#include "csapp.h"
#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

struct uri {
	char scheme[MAXLINE];
	char host[MAXLINE];
	char port[MAXLINE];
	char path[MAXLINE];
};

void *doit(void *vargp);
void read_requesthdrs(rio_t *rp);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
struct uri *parse_uri(char *uri);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
        "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
	printf("%s", user_agent_hdr);
	int listenfd;
	int *connfdp;
	pthread_t tid;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;

	/* Check command line args */
	if(argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = Open_listenfd(argv[1]);
	while(1) {
		clientlen = sizeof(clientaddr);
		connfdp   = Malloc(sizeof(int));
		*connfdp  = Accept(listenfd, (SA *) &clientaddr,
		                   &clientlen);// line:netp:tiny:accept
		Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
		            0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		Pthread_create(&tid, NULL, doit, connfdp);
		//doit(connfd); // line:netp:tiny:doit
		//Close(connfd);// line:netp:tiny:close
	}
	return 0;
}

void *doit(void *vargp) {
	int fd = *((int *) vargp);
	Pthread_detach(pthread_self());
	Free(vargp);

	struct stat sbuf;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];
	rio_t rio;

	/* Read request line and headers */
	Rio_readinitb(&rio, fd);
	if(!Rio_readlineb(&rio, buf, MAXLINE))// line:netp:doit:readrequest
		return;
	printf("%s", buf);
	sscanf(buf, "%s %s %s", method, uri, version);// line:netp:doit:parserequest
	if(strcasecmp(method, "GET")) {               // line:netp:doit:beginrequesterr
		clienterror(fd, method, "501", "Not Implemented",
		            "Tiny does not implement this method");
		return;
	}// line:netp:doit:endrequesterr
	struct uri *s_uri = parse_uri(uri);
	if(s_uri == NULL) {
		clienterror(fd, method, "400", "Bad Request",
		            "");
		return;
	}

	rio_t rio_out;
	int clientfd = Open_clientfd(s_uri->host, s_uri->port);
	Rio_readinitb(&rio_out, clientfd);
	char buf_out[MAXLINE];
	sprintf(buf_out, "%s %s HTTP/1.0\r\n", method, s_uri->path);
	Rio_writen(clientfd, buf_out, strlen(buf_out));
	char key[MAXLINE];
	int host_read   = 0;
	int header_sent = 0;
	while(1) {
		Rio_readlineb(&rio, buf_out, MAXLINE);
		if(strcmp(buf_out, "\r\n") == 0) {
			Rio_writen(clientfd, buf_out, strlen(buf_out));
			break;
		}
		int flag = 0;
		for(int i = 0; i < MAXLINE && buf_out[i] != '\0'; i++) {
			if(buf_out[i] == ':') {
				memcpy(key, buf_out, i);
				key[i] = '\0';
				flag   = 1;
				break;
			}
		}
		if(flag) {
			if(strcmp(key, "Connection") != 0) {
				continue;
			} else if(strcmp(key, "Proxy-Connection") != 0) {
				continue;
			} else if(strcmp(key, "User-Agent") != 0) {
				continue;
			} else if(strcmp(key, "Host") != 0) {
				host_read = 1;
				Rio_writen(clientfd, buf_out, strlen(buf_out));
				continue;
			} else {
				Rio_writen(clientfd, buf_out, strlen(buf_out));
			}
		} else if(header_sent == 0) {
			header_sent = 1;
			sprintf(buf_out, "%sConnection: close\r\nProxy-Connection: close\r\n", user_agent_hdr);
			Rio_writen(clientfd, buf_out, strlen(buf_out));
			if(host_read == 0) {
				sprintf(buf_out, "Host: %s\r\n", s_uri->host);
				Rio_writen(clientfd, buf_out, strlen(buf_out));
			}
		}
	}
	int n = 0;
	while((n = rio_readlineb(&rio_out, buf_out, MAXLINE)) != 0) {
		rio_writen(fd, buf_out, n);
	}
	Close(fd);
	return NULL;
}

struct uri *parse_uri(char *uri) {
	struct uri *ret = (struct uri *) malloc(sizeof(struct uri));
	int flag        = 1;
	int n           = strlen(uri);
	if(n > 0) {
		if(uri[n - 1] != '/') {
			uri[n]     = '/';
			uri[n + 1] = '\0';
		}
	} else {
		free(ret);
		return NULL;
	}
	int i = 0;
	int j = 0;
	for(i = 0; i < n; i++) {
		if(!isspace(uri[i])) {
			flag = 0;
			break;
		}
	}
	if(flag) {
		free(ret);
		return NULL;
	}
	flag = 1;
	for(j = i; j < n; j++) {
		if(uri[j] == ':') {
			memcpy(ret->scheme, uri + i, j - i);
			ret->scheme[j - i] = '\0';
			flag               = 0;
			break;
		}
	}
	if(flag) {
		free(ret);
		return NULL;
	}
	j += 3;
	i    = j;
	flag = 1;
	for(; j < n; j++) {
		if(uri[j] == ':' || uri[j] == '/') {
			memcpy(ret->host, uri + i, j - i);
			ret->host[j - i] = '\0';
			flag             = 0;
			break;
		}
	}
	if(flag) {
		free(ret);
		return NULL;
	}
	if(uri[j] == ':') {
		j++;
		i    = j;
		flag = 1;
		for(; j < n; j++) {
			if(uri[j] == '/') {
				memcpy(ret->port, uri + i, j - i);
				ret->port[j - i] = '\0';
				flag             = 0;
				break;
			}
		}
		if(flag) {
			free(ret);
			return NULL;
		}
	} else {
		memcpy(ret->port, "80", 3);
	}
	i = j;
	j = n;
	memcpy(ret->path, uri + i, j - i);
	ret->path[j - i] = '\0';
	return ret;
}

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp) {
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
	while(strcmp(buf, "\r\n")) {// line:netp:readhdrs:checkterm
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
	}
	return;
}
/* $end read_requesthdrs */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
	char buf[MAXLINE];

	/* Print the HTTP response headers */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n\r\n");
	Rio_writen(fd, buf, strlen(buf));

	/* Print the HTTP response body */
	sprintf(buf, "<html><title>Tiny Error</title>");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "<body bgcolor="
	             "ffffff"
	             ">\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
	Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */