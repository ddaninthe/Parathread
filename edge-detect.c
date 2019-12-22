#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bitmap.h"
#include <stdint.h>
#include <string.h>
#include <dirent.h>

/*
	//gcc edge-detect.c bitmap.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all
	//UTILISER UNIQUEMENT DES BMP 24bits
*/

#define NB_PARAMS 5
#define STACK_MAX 10
#define DIM 3
#define LENGTH DIM
#define OFFSET DIM /2

const float KERNEL[DIM][DIM] = {{-1, -1,-1},
							   {-1,8,-1},
							   {-1,-1,-1}};

typedef struct Color_t {
	float Red;
	float Green;
	float Blue;
} Color_e;

typedef struct stack_t {
        int data[STACK_MAX];
        int count;
        int max;
        pthread_mutex_t lock;
        pthread_cond_t can_consume;
        pthread_cond_t can_produce;
} Stack;

//on pourrait passer une structure en parametres
//pour eviter les variables globales
static Stack stack;

void apply_effect(Image* original, Image* new_i);
int bmpInFolder(char *dirname);
void emptyDir(char* path);

void apply_effect(Image* original, Image* new_i) {

	int w = original->bmp_header.width;
	int h = original->bmp_header.height;

	*new_i = new_image(w, h, original->bmp_header.bit_per_pixel, original->bmp_header.color_planes);

	for (int y = OFFSET; y < h - OFFSET; y++) {
		for (int x = OFFSET; x < w - OFFSET; x++) {
			Color_e c = { .Red = 0, .Green = 0, .Blue = 0};

			for(int a = 0; a < LENGTH; a++){
				for(int b = 0; b < LENGTH; b++){
					int xn = x + a - OFFSET;
					int yn = y + b - OFFSET;

					Pixel* p = &original->pixel_data[yn][xn];

					c.Red += ((float) p->r) * KERNEL[a][b];
					c.Green += ((float) p->g) * KERNEL[a][b];
					c.Blue += ((float) p->b) * KERNEL[a][b];
				}
			}

			Pixel* dest = &new_i->pixel_data[y][x];
			dest->r = (uint8_t)  (c.Red <= 0 ? 0 : c.Red >= 255 ? 255 : c.Red);
			dest->g = (uint8_t) (c.Green <= 0 ? 0 : c.Green >= 255 ? 255 : c.Green);
			dest->b = (uint8_t) (c.Blue <= 0 ? 0 : c.Blue >= 255 ? 255 : c.Blue);
		}
	}
}

int bmpInFolder(char* path) {
	DIR *dir = opendir(path);
	if (dir == NULL) return 0;

	int n = 0;
	struct dirent *file;
	while ((file = readdir(dir)) != NULL) {
		char* extension = strrchr(file->d_name, '.');
		if (extension && !strcmp(extension, ".bmp")) {
			n++;
		}
	}
	closedir(dir);
	return n;
}

void emptyDir(char* path) {
	DIR *dir = opendir(path);
	if (dir == NULL) return;

    struct dirent *file;    
    char filepath[256];
    while ((file = readdir(dir)) != NULL )
    {
        sprintf(filepath, "%s/%s", path, file->d_name);
        remove(filepath);
    }
    closedir(dir);
}

static Stack stack;

int main(int argc, char** argv) {
	if (argc < NB_PARAMS) {
        printf("Too few arguments.\nEx: ./apply-effect ./in/ ./out/ 3 boxblur\n");
        return 0;
    }
    
    // Input folder
    char* inputFolder = argv[1];
    int nbImages = bmpInFolder(inputFolder);
    if (nbImages < 1) {
    	printf("No image to process.\n");
    	return 0;
    }
    printf("Images to process: %d\n", nbImages);
    
    // Output folder
    char* outputFolder = argv[2];
    emptyDir(outputFolder);

    // Thread number
    int threadCount = atoi(argv[3]);
    if (threadCount <= 0 || threadCount > nbImages) {
        printf("Invalid number of threads: %d\n", threadCount);
        return 0;
    }
    printf("Number of threads: %d\n", threadCount);
    
    // Algorithm
    char* algo = argv[4];
    if (strcmp(algo, "boxblur") == 0) {
        printf("Boxblur\n");
    } else if (strcmp(algo, "edgedetect") == 0) {
        printf("Edge Detect\n");
    } else if (strcmp(algo, "sharpen") == 0) {
        printf("Sharpen\n");
    } else {
        printf("Invalid algorithm. Please use one of the followings: boxblur, edgedetect, sharpen\n");
        return 0;
    }
    
	/*pthread_t threads[PRODUCER_COUNT + 1];
	stack.count = 0;

	pthread_mutex_init(stack.lock);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);	

    for(int i = 0; i < PRODUCER_COUNT; i++) {
        pthread_create(&threads_id[i], &attr, producer, NULL);
	}*/

	/*Image img = open_bitmap("bmp_tank.bmp");
	Image new_i;
	apply_effect(&img, &new_i);
	save_bitmap(new_i, "test_out.bmp");
*/
	//pthread_mutex_destroy(stack.lock);

	return 0;
}