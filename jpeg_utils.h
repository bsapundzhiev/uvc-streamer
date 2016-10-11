#ifndef _JPEG_UTILS_H
#define _JPEG_UTILS_H

int compress_yuyv_to_jpeg(struct vdIn *vd, unsigned char *buffer, int size, int quality);
int compress_rggb_to_jpeg(struct vdIn *src, unsigned char* buffer, int size, int quality);
int compress_rgb_to_jpeg(struct vdIn *src, unsigned char* buffer, int size, int quality);

#endif

