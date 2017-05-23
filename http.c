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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>

#include "md5.h"
#include "cqueue.h"
#include "http.h"

#define SNAPSHOT_HEADER "HTTP/1.0 200 OK\r\n" \
  "Server: UVC Streamer\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "Content-type: image/jpeg\r\n" \
  "\r\n"

#define STREAM_HEADER "HTTP/1.0 200 OK\r\n" \
  "Server: UVC Streamer\r\n" \
  "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n" \
  "Cache-Control: no-cache\r\n" \
  "Cache-Control: private\r\n" \
  "Pragma: no-cache\r\n" \
  "Access-Control-Allow-Origin: *\r\n" \
  "\r\n"

#define STREAM_HEADER_CHUNK "--" BOUNDARY "\n" \
  "Content-type: image/jpeg\n\n"

#define AUTH_HEADER "HTTP/1.1 401 Unauthorized\n"\
  "Access-Control-Allow-Origin: *\r\n" \
  "WWW-Authenticate: Digest    realm=\"%s\",\n"\
  "        algorithm=MD5,\n"\
  "        nonce=\"%u\",\n"\
  "        opaque=\"%s\"\n\n"

static char bad_request_response[] =
  "HTTP/1.0 400 Bad Request\n"
  "Content-type: text/html\n"
  "Server: UVC Streamer\n"
  "\n"
  "<html>\n"
  " <body>\n"
  "  <h1>Bad Request</h1>\n"
  "  <p>This server did not understand your request.</p>\n"
  " </body>\n"
  "</html>\n";

static char unautorized_request_response[] =
  "HTTP/1.0 401 Unauthorized\n"
  "Content-type: text/html\n"
  "Server: UVC Streamer\n"
  "\n"
  "<html>\n"
  " <body>\n"
  "  <h1>Unauthorized</h1>\n"
  "  <p>Invalid credentials.</p>\n"
  " </body>\n"
  "</html>\n";

static char not_found_request_response[] =
  "HTTP/1.1 404 Not Found\n"
  "Content-type: text/html\n"
  "Server: UVC Streamer\n"
  "\n"
  "<html>\n"
  "  <body>\n"
  "   <h1>Not Found</h1>\n"
  "   <p>The requested URL %s was not found on this server.</p>\n"
  "  </body>\n"
  "</html>\n";

#define BOUNDARY      "arflebarfle"
#define REALM         "Private"
#define NONCE         1234
#define STREAM_URI    "/stream.mjpeg"
#define SNAPSHOT_URI  "/snapshot.jpeg"
#define BUFF_MAX      1024
#define READ_TIMEOUT  30

extern int stop;


struct http_header {
  char *method;
  char *uri;
  char *auth;
};

struct http_digest_auth {
  char *realm;
  uint32_t nonce;
  char opaque[33];
};

static void http_header_free(struct http_header *header);

static int print_picture(int fd, unsigned char *buf, int size)
{
  int jpg_hdr = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
  if(jpg_hdr != 0xFFD8FFE0 && jpg_hdr != 0xFFD8FFC0) {
      printf("%s: invalid JPEG header 0x%X\n", __func__, jpg_hdr);
  }
  if( write(fd, buf, size) <= 0) return -1;
  return 0;
}

uint32_t FNV_hash32(uint32_t key)
{
  uint8_t i, *bytes = (uint8_t*) (&key);
  uint32_t hash = 2166136261U;

  for (i = 0; i < sizeof(key); i++) {
    hash = (16777619U * hash) ^ bytes[i];
  }

  return hash;
}

static void md5str(const char *data, int len, char *md5string) {
  int i;
  MD5_CTX ctx;
  unsigned char digest[16];
  MD5_Init(&ctx);
  MD5_Update(&ctx, data, len);
  MD5_Final(digest, &ctx);

  for(i = 0; i < 16; ++i) {
    snprintf(&md5string[i*2], 1, "%02x", (unsigned int)digest[i]);
  }
}

static int data_available(int socket, int timeout)
{
  struct timeval to;
  fd_set fds;
  to.tv_sec = timeout, to.tv_usec = 0;
  FD_ZERO(&fds);
  FD_SET(socket, &fds);
  return select(socket+1, &fds, NULL, NULL, &to);
}

static int http_header_readline(int fd, char *buf, int len)
{
  char *ptr = buf;
  char *ptr_end = ptr + len - 1;

  if(data_available(fd, READ_TIMEOUT) <= 0) {
    return -1;
  }

  while (ptr < ptr_end) {
    switch (read(fd, ptr, 1)) {
    case 1:
      if (*ptr == '\r')
        continue;
      else if (*ptr == '\n') {
        *ptr = '\0';
        return ptr - buf;
      } else {
        ptr++;
        continue;
      }
    case 0:
      *ptr = '\0';
      return ptr - buf;
    default:
      printf("%s() failed: %s\n", __func__, strerror(errno));
      return -1;
    }
  }

  return len;
}

static int http_parse_headers(struct http_header *header, const char *hdr_line)
{
  char *p;
  const char * header_name = hdr_line;
  char * header_content = p = strchr(header_name, ':');

  if (!p)  {
    return -1;
  }

  *p = '\0';
  do {
    header_content++;
  } while (*header_content == ' ');

  if(!strcmp("Authorization", header_name)){
    header->auth = strdup(header_content);
  }

  *p = ':';
  return 0;
}

static int http_parse_header(struct clientArgs *client, struct http_header *header)
{
  char header_line[BUFF_MAX];
  char *token = NULL;
  int res, count =0;

  header->method= NULL;
  header->uri = NULL;
  header->auth = NULL;

  client->auth_state = AUTH_NONE;
  client->request_type = UNKNOWN;

  /* fcntl(socketfd, F_SETFL, fcntl(socketfd, F_GETFL, 0) | O_NONBLOCK);*/
  while ((res = http_header_readline( client->socket, header_line, sizeof(header_line))) > 0) {
    if (!count) {
      token = strtok(header_line, " ");
      if(token) {
        header->method = strdup(token);
      }
      token = strtok(NULL, " ");
      if(token) {
        header->uri = strdup(token);
      }

    } else {
      http_parse_headers(header, header_line);
    }
    count++;
  }

  if (header->uri) {
    if (!strcmp(header->uri, SNAPSHOT_URI)) {
      client->request_type = SNAPSHOT;
    }
    if (!strcmp(header->uri, STREAM_URI)) {
      client->request_type = STREAM;
    }
  } else {
    client->request_type= INVALID;
  }

  if (client->server->password) {
    client->auth_state = (header->auth == NULL) ? AUTH_PENDING : AUTH_CHECK;
  }

  return res;
}

static void http_header_free(struct http_header *header)
{
  if(header->method){
    free(header->method);
  }

  if(header->uri){
    free(header->uri);
  }

  if(header->auth){
    free(header->auth);
  }
}

void http_digest_init(struct http_digest_auth *auth)
{
  auth->realm = REALM;
  auth->nonce = FNV_hash32(NONCE);
  md5str(auth->realm, strlen(auth->realm), auth->opaque);
}

int http_digest_responce(struct clientArgs *client,
                        struct http_digest_auth *auth,
                        struct http_header *hdr)
{
  char A1[33], A2[33];
  char response_hash[33], buff[256];
  snprintf(buff, sizeof(buff)-1, "%s:%s:%s", client->server->username,
    auth->realm, client->server->password);
  md5str(buff, strlen(buff), A1);
  snprintf(buff, sizeof(buff)-1, "%s:%s", hdr->method, hdr->uri);
  md5str(buff, strlen(buff), A2);
  snprintf(buff, sizeof(buff)-1, "%s:%u:%s", A1, auth->nonce, A2);
  md5str(buff, strlen(buff), response_hash);

  return (strstr(hdr->auth, response_hash) != NULL);
}

/* thread for clients that connected to this server */
static void *http_client_thread( void *arg )
{
  struct clientArgs *ca = (struct clientArgs *)arg;
  struct thread_buff *tbuff = ca->server->ptbuff;
  int ok = 1, should_close_connection = 0;
  char buffer[BUFF_MAX] = {0};
  struct buff *b = NULL;
  struct http_header header;
  struct http_digest_auth digest_auth;

  http_digest_init(&digest_auth);
  pthread_detach(pthread_self());

  if(http_parse_header(ca, &header) < 0){
    close(ca->socket);
    http_header_free(&header);
    free(arg);
    return NULL;
  }

  printf("thread_id: %ld request %s\n", pthread_self(), header.uri);

  switch (ca->request_type) {
    case SNAPSHOT:
      snprintf(buffer, sizeof(buffer), SNAPSHOT_HEADER);
    break;
    case STREAM:
      snprintf(buffer, sizeof(buffer), STREAM_HEADER);
    break;
    case INVALID:
      snprintf(buffer, sizeof(buffer), "%s", bad_request_response);
      should_close_connection = 1;
    break;
    default:
      snprintf(buffer, sizeof(buffer), not_found_request_response, header.uri);
      should_close_connection = 1;
    break;
  }

  switch (ca->auth_state) {
    case AUTH_PENDING:
      snprintf(buffer, sizeof(buffer), AUTH_HEADER,
              digest_auth.realm, digest_auth.nonce, digest_auth.opaque);
      should_close_connection = 1;
    break;
    case AUTH_CHECK:
      if(!http_digest_responce(ca, &digest_auth, &header)) {
        snprintf(buffer, sizeof(buffer), "%s", unautorized_request_response);
        should_close_connection = 1;
      }
    break;
    default:
    break;
  }

  ok = ( write(ca->socket, buffer, strlen(buffer)) >= 0)?1:0;

  if (should_close_connection) {
    close(ca->socket);
    http_header_free(&header);
    free(arg);
    return NULL;
  }

  /* mjpeg server push */
  while (ok >= 0 && !stop) {

    pthread_mutex_lock(&(tbuff)->lock);
    pthread_cond_wait(&(tbuff)->cond, &(tbuff)->lock);

    b = queue_front(&(tbuff)->qbuff);

    if (ca->request_type == STREAM) {
      snprintf(buffer,sizeof(buffer), STREAM_HEADER_CHUNK);

      if(write(ca->socket, buffer, strlen(buffer)) < 0) {
        pthread_mutex_unlock( &(tbuff)->lock );
        break;
      }
    }

    ok = print_picture(ca->socket, b->buff, b->size);
    pthread_mutex_unlock( &(tbuff)->lock );

    if(ca->request_type != STREAM) {
      break;
    }
  }

  close(ca->socket);
  http_header_free(&header);
  free(arg);
  return NULL;
}


int http_listener(struct http_server *srv)
{
  struct sockaddr_in addr;
  int on=1;
  int c = sizeof(struct sockaddr_in);
  struct clientArgs *ca;
  /* open socket for server */
  srv->sd = socket(PF_INET, SOCK_STREAM, 0);
  if ( srv->sd < 0 ) {
    fprintf(stderr, "socket failed\n");
    exit(1);
  }

  /* ignore "socket already in use" errors */
  if (setsockopt(srv->sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    perror("setsockopt(SO_REUSEADDR) failed");
    exit(1);
  }

  /* configure server address to listen to all local IPs */
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = srv->port;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if ( bind(srv->sd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ) {
    perror("Bind");
    exit(1);
  }

  /* start listening on socket */
  if (listen(srv->sd, 10) != 0 ) {
    fprintf(stderr, "listen failed\n");
    exit(1);
  }

  srv->client_thread = http_client_thread;
  while( 1 ) {
    /* alloc new client */
    ca = malloc(sizeof(struct clientArgs));
    ca->server = srv;
    ca->socket = accept(srv->sd, (struct sockaddr *)&ca->client_addr, (socklen_t*)&c);
    if (ca->socket < 0) {
      perror("accept failed");
      free(ca);
      continue;
    }

    if( pthread_create(&srv->client, NULL,  srv->client_thread, ca) < 0) {
      perror("could not create client thread");
      return 1;
    }
  }
}
