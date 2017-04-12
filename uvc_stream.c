/*******************************************************************************
# "uvc_stream" is a command line application to stream JPEG files over an      #
# IP-based network from the webcam to a viewer like Firefox, Cambozola,        #
# Videolanclient or even to a Windows Mobile device running the TCPMP-Player.  #
#                                                                              #
# It was written for embedded devices with very limited ressources in terms of #
# RAM and CPU. The decision for Linux-UVC was done, because supported cameras  #
# directly produce JPEG-data, allowing fast and perfomant M-JPEG streams even  #
# from slow embedded devices like those supported by OpenWRT.                  #
#                                                                              #
# I would suggest not to open this server to the internet. Use it as input to  #
# the programm "motion" [http://motion.sf.net] running at your DMZ instead.    #
# Motion has many users and i expect it to be checked more often for security  #
# issues. Keep in mind, that motions advanced capabilties like                 #
# motion-detection, writing of avifiles etc require much more ressources.      #
#                                                                              #
# In contrast to the better known SPCA5XX-LE, UVC-cameras in average produce   #
# better image quality (See Michel XHaards comparison table and rating at      #
# his site) [http://mxhaard.free.fr/embedded.html].                            #
#                                                                              #
# This programm was written in 2007 by Tom Stöveken, basing on luvcview.       #
# The luvcview sources were modified using the tool "indent" and afterwards    #
# SDL dependencies were removed to reduce dependencies to other packages.      #
#                                                                              #
# This package work with the Logitech UVC based webcams with the mjpeg feature.#
#                                                                              #
#     Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard               #
#     Copyright (C) 2007      Tom Stöveken                                     #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; version 2 of the License.                      #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/types.h>          /* for videodev2.h */
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <pthread.h>

#include "v4l2uvc.h"
#include "jpeg_utils.h"
#include "cqueue.h"
#include "http.h"
#include "avilib.h"

#define SOURCE_VERSION "1.0.1"
#define VIDEODEV "/dev/video0"
#define NELEMS(x) (sizeof(x) / sizeof((x)[0]))
#define QMAX 3
#define SERVER_USER "uvc_user"

struct control_data {
  struct vdIn *videoIn;
  int width;
  int height;
  int video_dev;
  int quality;
  int fps, daemon;
  int format;
  char *filename;
  pthread_t tcam;
  pthread_t trecorder;
};

struct pixel_format {
  char *name;
  int format;
};

struct resolutions{
  char *name;
  int width;
  int height;
};

struct pixel_format pixel_formats[] = {
    {"MJPG",  V4L2_PIX_FMT_MJPEG  },
    {"JPEG",  V4L2_PIX_FMT_JPEG   },
    {"YUYV",  V4L2_PIX_FMT_YUYV   },
    {"RGGB",  V4L2_PIX_FMT_SRGGB8 },
    {"RGB24", V4L2_PIX_FMT_RGB24  },
};

struct resolutions resolutions_formats[] = {
  /*https://en.wikipedia.org/wiki/Display_resolution#/media/File:Vector_Video_Standards8.svg*/
  {"1280x720", 1280, 720  },
  {"960x720",   960, 720  },
  {"800x600",   800, 600  },
  {"854x480",   854, 480  },
  {"800x480",   800, 480  },
  {"768x576",   768, 576  },
  {"640x480",   640, 480  },
  {"480x320",   480, 320  },
  {"384x288",   384, 288  },
  {"352x288",   352, 288  },
  {"320x240",   320, 240  },
  {"320x200",   320, 200  },
  {"160x120",   160, 120  },
};

int stop=0;
struct control_data cd;
struct thread_buff tbuff = {
  PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_COND_INITIALIZER,
  {0,0},
};

static void print_version(void);
static void help(char *progname);

/* the single writer thread */
static void *cam_thread( void *arg ) {

  struct thread_buff *tbuff = (struct thread_buff*)arg;
  struct buff * b = NULL;

  while( !stop ) {
    /* grab a frame */
    if( uvcGrab(cd.videoIn) < 0 ) {
      fprintf(stderr, "Error grabbing\n");
      exit(1);
    }

    /* copy frame to global buffer */
    pthread_mutex_lock( &tbuff->lock );

   /*
    * If capturing in YUV mode convert to JPEG now.
    * This compression requires many CPU cycles, so try to avoid YUV format.
    * Getting JPEGs straight from the webcam, is one of the major advantages of
    * Linux-UVC compatible devices.
    */
    b = queue_pop(&(tbuff)->qbuff);

    if(cd.videoIn->formatIn == V4L2_PIX_FMT_YUYV) {

      b->size = compress_yuyv_to_jpeg(cd.videoIn, b->buff, cd.videoIn->framesizeIn, cd.quality);
    }
    else if(cd.videoIn->formatIn == V4L2_PIX_FMT_SRGGB8) {

      b->size = compress_rggb_to_jpeg(cd.videoIn, b->buff, cd.videoIn->framesizeIn, cd.quality);
    }
    else if(cd.videoIn->formatIn == V4L2_PIX_FMT_RGB24) {

      b->size = compress_rgb_to_jpeg(cd.videoIn, b->buff, cd.videoIn->framesizeIn, cd.quality);
    }
    else {
      b->size = cd.videoIn->framesizeIn;
      memcpy(b->buff, cd.videoIn->tmpbuffer, cd.videoIn->framesizeIn);
    }

    queue_push(&(tbuff)->qbuff, b);
    /* signal fresh_frame */
    pthread_cond_broadcast(&tbuff->cond);
    pthread_mutex_unlock(&tbuff->lock);

    /* only use usleep if the fps is below 5, otherwise the overhead is too long */
    if ( cd.videoIn->fps < 5 ) {
      usleep(1000*1000/cd.videoIn->fps);
    }
  }
  printf("Exit cam thread\n");
  pthread_exit(NULL);
}

static void *video_recoreder_thread(void *arg)
{
  struct vdIn *vd = cd.videoIn;

  struct thread_buff *tbuff = (struct thread_buff*)arg;
  struct buff * b = NULL;

  avi_t *avifile = AVI_open_output_file(cd.filename);

  if (avifile == NULL ) {
    fprintf(stderr,"Error opening avifile %s\n", cd.filename);
    exit(-1);
  }

  AVI_set_video(avifile, vd->width, vd->height, vd->fps, "MJPG");
  printf("recording to %s\n", cd.filename);

  while(!stop) {

    pthread_mutex_lock(&(tbuff)->lock);
    pthread_cond_wait(&(tbuff)->cond, &(tbuff)->lock);

    b = queue_front(&(tbuff)->qbuff);
    AVI_write_frame(avifile, (char*)b->buff, b->size, vd->framecount);
    vd->framecount++;

    pthread_mutex_unlock(&(tbuff)->lock);
  }
  printf("exit vr thread\n");
  AVI_close(avifile);
  pthread_exit(NULL);
}

static void signal_handler(int sigm) {
  /* signal "stop" to threads */
  stop = 1;
  /* cleanup most important structures */
  fprintf(stderr, "Shutdown...\n");
  pthread_cond_broadcast(&tbuff.cond);
  pthread_join(server.client, NULL);
  usleep(1000 * 1000);
  pthread_join(cd.tcam, NULL);
  close_v4l2(cd.videoIn);
  free(cd.videoIn);
  if (close (server.sd) < 0) {
	  perror ("close sd");
  }
  pthread_cond_destroy(&tbuff.cond);
  pthread_mutex_destroy(&tbuff.lock);
  exit(0);
}

static void daemon_mode(void) {
  int fr=0;

  fr = fork();
  if( fr < 0 ) {
    fprintf(stderr, "fork() failed\n");
    exit(1);
  }
  if ( fr > 0 ) {
    exit(0);
  }

  if( setsid() < 0 ) {
    fprintf(stderr, "setsid() failed\n");
    exit(1);
  }

  fr = fork();
  if( fr < 0 ) {
    fprintf(stderr, "fork() failed\n");
    exit(1);
  }
  if ( fr > 0 ) {
    fprintf(stderr, "forked to background (%d)\n", fr);
    exit(0);
  }

  umask(0);
}

/* Main */
int main(int argc, char *argv[])
{
  char *dev = VIDEODEV;
  char *fmtStr = "UNKNOWN";
  int i;
  cd.format = V4L2_PIX_FMT_MJPEG;
  cd.fps= 5;
  cd.daemon = 0;
  cd.width=640;
  cd.height=480;
  server.port = htons(8080);
  cd.quality = 40;
  server.username = SERVER_USER;

  while(1) {
    int option_index = 0, c=0;
    static struct option long_options[] = \
    {
      {"h", no_argument, 0, 0},
      {"help", no_argument, 0, 0},
      {"d", required_argument, 0, 0},
      {"device", required_argument, 0, 0},
      {"r", required_argument, 0, 0},
      {"resolution", required_argument, 0, 0},
      {"f", required_argument, 0, 0},
      {"fps", required_argument, 0, 0},
      {"p", required_argument, 0, 0},
      {"port", required_argument, 0, 0},
      {"u", required_argument, 0, 0},
      {"P", required_argument, 0, 0},
      {"y", no_argument, 0, 0},
      {"g", no_argument, 0, 0},
      {"q", required_argument, 0, 0},
      {"v", no_argument, 0, 0},
      {"version", no_argument, 0, 0},
      {"b", no_argument, 0, 0},
      {"background", no_argument, 0, 0},
      {"o", required_argument, 0, 0},
      {0, 0, 0, 0}
    };

    c = getopt_long_only(argc, argv, "", long_options, &option_index);

    /* no more options to parse */
    if (c == -1) break;

    /* unrecognized option */
    if(c=='?'){ help(argv[0]); return 0; }

    switch (option_index) {
      /* h, help */
      case 0:
      case 1:
        help(argv[0]);
        return 0;
        break;

      /* d, device */
      case 2:
      case 3:
        dev = strdup(optarg);
        break;

      /* r, resolution */
      case 4:
      case 5:
          for(i = 0; i < NELEMS(resolutions_formats); i++){
            if(!strcmp(resolutions_formats[i].name, optarg)) {
                cd.width = resolutions_formats[i].width;
                cd.height = resolutions_formats[i].height;
            }
          }
        break;

      /* f, fps */
      case 6:
      case 7:
        cd.fps = atoi(optarg);
        break;

      /* p, port */
      case 8:
      case 9:
        server.port = htons(atoi(optarg));
        break;
      /*u*/
      case 10:
        server.username = optarg;
        break;
      /*P*/
      case 11:
        server.password = optarg;
        break;
      /* y */
      case 12:
        cd.format = V4L2_PIX_FMT_YUYV;
        break;
      /* g */
      case 13:
        cd.format = V4L2_PIX_FMT_SRGGB8;
        break;
      /* q */
      case 14:
        cd.quality = atoi(optarg);
        break;
      /* v, version */
      case 15:
      case 16:
        print_version();
        return 0;
      /* b, background */
      case 17:
      case 18:
        cd.daemon = 1;
        break;
      case 19:
        cd.filename = optarg;
        break;
      default:
        help(argv[0]);
        return 0;
    }
  }

  /* ignore SIGPIPE (send if transmitting to closed sockets) */
  signal(SIGPIPE, SIG_IGN);
  if (signal(SIGINT, signal_handler) == SIG_ERR) {
    fprintf(stderr, "could not register signal handler\n");
    exit(1);
  }

  /* allocate webcam datastructure */
  cd.videoIn = (struct vdIn *) calloc(1, sizeof(struct vdIn));

  for(i = 0; i < NELEMS(pixel_formats); i++){
    if(pixel_formats[i].format == cd.format) {
        fmtStr = pixel_formats[i].name;
    }
  }

  fprintf(stderr, "Using V4L2 device: %s\n", dev);
  fprintf(stderr, "Format: %s\n", fmtStr);
  fprintf(stderr, "JPEG quality: %i\n", cd.quality);
  fprintf(stderr, "Resolution: %i x %i @ %i fps\n", cd.width, cd.height, cd.fps);
  fprintf(stderr, "TCP port: %i user: %s pass: %s\n", ntohs(server.port),
                  server.username, server.password ? "*****" : "none !!!");
  /* open video device and prepare data structure */
  cd.video_dev = init_videoIn(cd.videoIn, dev, cd.width, cd.height, cd.fps, cd.format, 1);
  if (cd.video_dev < 0) {
    fprintf(stderr, "init_VideoIn failed\n");
    exit(1);
  }

  /* fork to the background */
  if ( cd.daemon ) {
    daemon_mode();
  }

  /* start to read the camera, push picture buffers into global buffer */
  init_queue(&tbuff.qbuff, QMAX);
  for(i=0; i  < QMAX; i++){
    struct buff *b = malloc(sizeof(b));
    b->buff =(unsigned char *) calloc(1, (size_t)cd.videoIn->framesizeIn);
    queue_push(&tbuff.qbuff, b);
  }

  pthread_create(&cd.tcam, NULL, cam_thread, &tbuff);
  pthread_detach(cd.tcam);

  if(cd.filename) {
    pthread_create(&cd.trecorder, NULL, video_recoreder_thread, &tbuff);
    pthread_join(cd.trecorder, NULL);
  } else {
    /*start http streamer */
    server.ptbuff = &tbuff;
    http_listener(&server);
  }

  return 0;
}

void help(char *progname)
{
  fprintf(stderr, "Usage: %s\n"
    " [-h, --help ]          display this help\n"
    " [-d, --device ]        video device to open (your camera)\n"
    " [-r, --resolution ]    e.g. 960x720, 640x480, 320x240, 160x120\n"
    " [-f, --fps ]           frames per second\n"
    " [-p, --port ]          TCP-port for the stream server\n"
    " [-u ]                  server user(default uvc_user)\n"
    " [-P ]                  server password\n"
    " [-y ]                  use YUYV format\n"
    " [-g ]                  use RGGB format\n"
    " [-q ]                  compression quality\n"
    " [-v | --version ]      display version information\n"
    " [-b | --background]    fork to the background, daemon mode\n"
    " [-o ]                  output filename (.avi)\n"
    "\n", progname);
}

void print_version()
{
  printf("UVC Streamer Version: %s (%s %s)\n"
    "Copyright (C) 2016 Borislav Sapundzhiev <bsapundjiev@gmail.com>\n"
    "Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard\n"
    "Copyright (C) 2007      Tom Stöveken\n"
    "License GPLv2: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n\n"
    "This is free software; you are free to change and redistribute it.\n"
    "There is NO WARRANTY, to the extent permitted by law.\n", SOURCE_VERSION, __DATE__, __TIME__);
}
