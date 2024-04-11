/*  http stream server
 *
 *  Copyright (C) 2016 by Borislav Sapundzhiev
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */
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

struct http_server {
  int sd;
  int port;
  char *username;
  char *password;
  struct thread_buff *ptbuff;
  pthread_t client;
  client_thread_t client_thread;
};

typedef enum { AUTH_NONE, AUTH_PENDING, AUTH_CHECK } auth_state_t;
typedef enum { UNKNOWN, INVALID, SNAPSHOT, STREAM } request_t;

struct clientArgs {
  int socket;
  struct sockaddr_in client_addr;
  struct http_server *server;
  auth_state_t auth_state;
  request_t request_type;
};

int http_listener(struct http_server *srv);

#endif
