#ifndef _UVC_HTTPD_H
#define _UVC_HTTPD_H

struct buff {
  int size;
  unsigned char *buff;
};

struct thread_buff {
  pthread_mutex_t lock;
  pthread_cond_t  cond;
  cqueue_t qbuff;
};

/* client thread type */
typedef void *(*client_thread_t)(void *);

struct clientArgs {
  int socket;
  char *username;
  char *password;
  struct thread_buff *ptbuff;
  struct sockaddr_in client_addr;
};

struct http_server {
  int sd;
  int port;
  pthread_t client;
  client_thread_t client_thread;
} server;

int http_listener(struct http_server *srv);
void *http_client_thread( void *arg );

#endif