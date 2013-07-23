#ifndef RESIZE_H_
#define RESIZE_H_

#include <stdio.h>

struct imgsrc;

struct imgdst;

struct imgsrc *memsrc_new(unsigned char *data, int width, int height, int bpp);

void memsrc_free(struct imgsrc *img);

struct imgdst *memdst_new(unsigned char *data, int width, int height, int bpp);

void memdst_free(struct imgdst *img);

struct imgsrc *fiosrc_new(FILE *file, int width, int height, int bpp,
		int row_caches);

void fiosrc_free(struct imgsrc *img);

struct imgdst *fiodst_new(FILE *file, int width, int height, int bpp);

void fiodst_free(struct imgdst *img);

int scale_up(unsigned char *buffer, int width, int height, int bpp, int rowsize,
		struct imgdst *dst);

int scale_down(struct imgsrc *src, struct imgdst *dst);

#endif /* RESIZE_H_ */
