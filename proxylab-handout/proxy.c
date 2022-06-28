#include "csapp.h"
#include <limits.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

struct cache_object {
	unsigned long seq;
	unsigned index;
	char uri[MAXLINE];
	char buf[MAX_CACHE_SIZE];
	int buf_len;
};

struct cache {
	unsigned long next_seq;
	struct cache_object *objects[MAX_OBJECT_SIZE];
	sem_t locks[MAX_OBJECT_SIZE];
} cache;

struct cache_object *get_cache(char *uri);

unsigned str_hash(char *str, int n);

struct uri {
	char scheme[MAXLINE];
	char host[MAXLINE];
	char port[MAXLINE];
	char path[MAXLINE];
};

void *doit(void *vargp);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
struct uri *parse_uri(char *uri);
void cache_insert(char *uri, char *buf, int buf_n);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
        "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
	cache.next_seq = 0;
	for(int i = 0; i < MAX_OBJECT_SIZE; i++) {
		sem_init(&cache.locks[i], 0, 1);
	}
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

	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	rio_t rio;

	/* Read request line and headers */
	Rio_readinitb(&rio, fd);
	if(!Rio_readlineb(&rio, buf, MAXLINE))// line:netp:doit:readrequest
		return NULL;
	printf("%s", buf);
	sscanf(buf, "%s %s %s", method, uri, version);// line:netp:doit:parserequest
	if(strcasecmp(method, "GET")) {               // line:netp:doit:beginrequesterr
		clienterror(fd, method, "501", "Not Implemented",
		            "Tiny does not implement this method");
		return NULL;
	}// line:netp:doit:endrequesterr
	struct cache_object *co = get_cache(uri);
	if(co != NULL) {
		printf("cache hit for %s\n", uri);
		Rio_writen(fd, co->buf, co->buf_len);
		V(&cache.locks[co->index]);
		Close(fd);
		return NULL;
	}
	struct uri *s_uri = parse_uri(uri);
	if(s_uri == NULL) {
		clienterror(fd, method, "400", "Bad Request",
		            "");
		return NULL;
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
	char cache_buf[MAX_CACHE_SIZE];
	unsigned off = 0;
	int flag     = 1;
	while((n = rio_readlineb(&rio_out, buf_out, MAXLINE)) != 0) {
		rio_writen(fd, buf_out, n);
		if(flag && off + n < MAX_CACHE_SIZE) {
			memcpy(cache_buf + off, buf_out, n);
			off += n;
		} else {
			flag = 0;
		}
	}
	Close(fd);
	if(flag) {
		cache_insert(uri, cache_buf, off);
	}
	return NULL;
}

struct uri *parse_uri(char *uri) {
	struct uri *ret = (struct uri *) malloc(sizeof(struct uri));
	int flag        = 1;
	int n           = strlen(uri);
	if(n <= 0) {
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
		if(uri[j] == ':' || uri[j] == '/' || j + 1 == n) {
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
			if(uri[j] == '/' || j + 1 == n) {
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

unsigned str_hash(char *str, int n) {
	unsigned long ans = 0;
	for(int i = 0; i < n; i++) {
		ans <<= 7;
		ans += str[i];
	}
	printf("hash for %s is %u\n", str, (unsigned) (ans % MAX_OBJECT_SIZE));
	return (unsigned) (ans % MAX_OBJECT_SIZE);
}

struct cache_object *get_cache(char *uri) {
	printf("getting cache for %s\n", uri);
	unsigned h = str_hash(uri, strlen(uri));
	for(int i = 0; i < MAX_OBJECT_SIZE; i++) {
		printf("current h = %u\n", h);
		P(&cache.locks[h]);
		if(cache.objects[h] == NULL) {
			printf("get_cache: cache miss\n");
			V(&cache.locks[h]);
			return NULL;
		}
		printf("this uri = %s\n", cache.objects[h]->uri);
		if(strcmp(cache.objects[h]->uri, uri) == 0) {
			//V(&cache.locks[h]); //release after handling return
			printf("get_cache: cache hit\n");
			return cache.objects[h];
		}
		V(&cache.locks[h]);
		h++;
		h %= MAX_OBJECT_SIZE;
	}
}

void cache_insert(char *uri, char *buf, int buf_n) {
	printf("start cache_insert for %s\n", uri);
	unsigned h = str_hash(uri, strlen(uri));

	struct cache_object *co = (struct cache_object *) malloc(sizeof(struct cache_object));
	co->seq                 = cache.next_seq++;
	co->buf_len             = buf_n;
	memcpy(co->uri, uri, strlen(uri));
	memcpy(co->buf, buf, buf_n);

	struct cache_object *to_evict;
	for(int i = 0; i < MAX_OBJECT_SIZE; i++) {
		int flag = 1;
		P(&cache.locks[h]);
		if(cache.objects[h] == NULL) {
			cache.objects[h] = co;
			co->index        = h;
			printf("cache for %s inserted to %u\n", uri, h);
			V(&cache.locks[h]);
			return;
		} else {
			if(to_evict == NULL) {
				to_evict = cache.objects[h];
			} else if(cache.objects[h]->seq < to_evict->seq) {
				V(&cache.locks[to_evict->index]);
				to_evict = cache.objects[h];
				flag     = 0;
			}
		}
		if(flag) {
			V(&cache.locks[h]);
		}
		h++;
		h %= MAX_OBJECT_SIZE;
	}
	cache.objects[to_evict->index] = co;
	V(&cache.locks[to_evict->index]);
	free(to_evict);
}