###############################################################
#
# Purpose: Makefile for "UVC Streamer"
# Author.: Tom Stoeveken (TST)
# Version: 0.0
# License: GPL (inherited from luvcview)
#
###############################################################


ifeq ($(CC),cc)
CC=gcc
CFLAGS += -O2 -DLINUX -Wall -pedantic
LFLAGS += -lpthread -ljpeg
else
CFLAGS += -O2 -DLINUX -Wall -pedantic -I ./jpeg-8
LFLAGS += -lpthread -L ./jpeg-8/.libs -ljpeg
endif

APP_BINARY=uvc_stream
OBJECTS=uvc_stream.o v4l2uvc.o jpeg_utils.o cqueue.o http.o md5.o avilib.o

all: uga_buga

clean:
	@echo "Cleaning up directory."
	rm -f *.a *.o $(APP_BINARY) core *~ log errlog *.avi

# Applications:
uga_buga: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(APP_BINARY) $(LFLAGS)
	chmod 755 $(APP_BINARY)

# useful to make a backup "make tgz"
tgz: clean
	mkdir -p backups
	tar czvf ./backups/uvc_streamer_`date +"%Y_%m_%d_%H.%M.%S"`.tgz --exclude backups *
