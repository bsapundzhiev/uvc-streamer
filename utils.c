/*******************************************************************************
# UVC streamer: Linuc-UVC streaming application                                #
#This package work with the Logitech UVC based webcams with the mjpeg feature. #
#                                                                              #
# Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard                   #
#                    2007 Tom Stöveken                                         #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; either version 2 of the License, or            #
# (at your option) any later version.                                          #
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

#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include <string.h>
#include <fcntl.h>
#include <wait.h>
#include <time.h>
#include <limits.h>
#include "huffman.h"

int is_huffman(unsigned char *buf)
{
    unsigned char *ptbuf;
    int i = 0;
    ptbuf = buf;
    while(((ptbuf[0] << 8) | ptbuf[1]) != 0xffda) {
        if(i++ > 2048)
            return 0;
        if(((ptbuf[0] << 8) | ptbuf[1]) == 0xffc4)
            return 1;
        ptbuf++;
    }
    return 0;
}

int memcpy_picture(unsigned char *out, unsigned char *buf, int size)
{
    unsigned char *ptdeb, *ptlimit, *ptcur = buf;
    int sizein, pos = 0;

    if(!is_huffman(buf)) {
        ptdeb = ptcur = buf;
        ptlimit = buf + size;
        while((((ptcur[0] << 8) | ptcur[1]) != 0xffc0) && (ptcur < ptlimit))
            ptcur++;
        if(ptcur >= ptlimit)
            return pos;
        sizein = ptcur - ptdeb;

        memcpy(out + pos, buf, sizein); pos += sizein;
        memcpy(out + pos, dht_data, sizeof(dht_data)); pos += sizeof(dht_data);
        memcpy(out + pos, ptcur, size - sizein); pos += size - sizein;
    } else {
        memcpy(out + pos, ptcur, size); pos += size;
    }
    return pos;
}

int print_picture(int fd, unsigned char *buf, int size)
{
    int jpg_hdr = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    if(jpg_hdr != 0xFFD8FFE0 && jpg_hdr != 0xFFD8FFC0) {
        printf("%s: invalid JPEG header 0x%X\n", __func__, jpg_hdr);
    }
    if( write(fd, buf, size) <= 0) return -1;
    return 0;
}
