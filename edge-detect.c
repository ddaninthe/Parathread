#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bitmap.h"
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>

/*
	//gcc edge-detect.c bitmap.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all
	//UTILISER UNIQUEMENT DES BMP 24bits
*/

#define NB_PARAMS 5
#define STACK_MAX 10
#define DIM 3
#define LENGTH DIM
#define OFFSET DIM /2

const float KERNEL[DIM][DIM] = {{-1, -1, -1},
							   {-1, 8, -1},
							   {-1, -1, -1}};
							   
const float BOXBLUR[DIM][DIM] = {{1.0f/9, 1.0f/9, 1.0f/9},
							   {1.0f/9, 1.0f/9, 1.0f/9},
							   {1.0f/9, 1.0f/9, 1.0f/9}};

const float SHARPEN[DIM][DIM] = {{0, -1, 0},
							   {-1, 5, -1},
							   {0, -1, 0}};

typedef struct Image_File_t {
	Image image;
	char* filename;
} Image_File;

typedef struct Color_t {
	float Red;
	float Green;
	float Blue;
} Color_e;

typedef struct Producer_Arg_t {
	char* inputFolder;
	char* algo
} Producer_Arg;

typedef struct stack_t {
        Image_File data[STACK_MAX];
        int count; // Current stack size
        int max; // Max size of stack
       	int nbFiles; // Number of file to convert
        pthread_mutex_t lock;
        pthread_cond_t can_consume;
        pthread_cond_t can_produce;
       	int conversionAmount; // Amount of file converted or being converted <= index for next file
       	char* allFilenames[]; // All filepaths to convert
} Stack;

//on pourrait passer une structure en parametres
//pour eviter les variables globales
static Stack stack;

void inline apply_convolution(Color_e* c, int a, int b, int x, int y, Image* img, char* algo) __attribute__((always_inline));
void apply_effect(Image* original, Image* new_i, char* algo);
int bmpInFolder(char *dirname);
void* consumer(void* arg);
void emptyDir(char* path);
void* producer(void* args);
void stack_destroy();
void stack_init();

void apply_convolution(Color_e* restrict c, int a, int b, int x, int y, Image* restrict img, char* algo) {
	int xn = x + a - OFFSET;
	int yn = y + b - OFFSET;

	Pixel* p = &img->pixel_data[yn][xn];
	
	if (!strcmp(algo, "edgedetect")) {
		c->Red += ((float) p->r) * KERNEL[a][b];
		c->Green += ((float) p->g) * KERNEL[a][b];
		c->Blue += ((float) p->b) * KERNEL[a][b];
	}
	else if (!strcmp(algo, "boxblur")) {
		c->Red += ((float) p->r) * BOXBLUR[a][b];
		c->Green += ((float) p->g) * BOXBLUR[a][b];
		c->Blue += ((float) p->b) * BOXBLUR[a][b];
	}
	else if (!strcmp(algo, "sharpen")) {
		c->Red += ((float) p->r) * SHARPEN[a][b];
		c->Green += ((float) p->g) * SHARPEN[a][b];
		c->Blue += ((float) p->b) * SHARPEN[a][b];
	}
}

void apply_effect(Image* original, Image* new_i, char* algo) {

	int w = original->bmp_header.width;
	int h = original->bmp_header.height;

	*new_i = new_image(w, h, original->bmp_header.bit_per_pixel, original->bmp_header.color_planes);

	for (int y = OFFSET; y < h - OFFSET; y++) {
		for (int x = OFFSET; x < w - OFFSET; x++) {
			Color_e c = { .Red = 0, .Green = 0, .Blue = 0};

			apply_convolution(&c, 0, 0, x, y, original, algo);
			apply_convolution(&c, 0, 1, x, y, original, algo);
			apply_convolution(&c, 0, 2, x, y, original, algo);

			apply_convolution(&c, 1, 0, x, y, original, algo);
			apply_convolution(&c, 1, 1, x, y, original, algo);
			apply_convolution(&c, 1, 2, x, y, original, algo);

			apply_convolution(&c, 2, 0, x, y, original, algo);
			apply_convolution(&c, 2, 1, x, y, original, algo);
			apply_convolution(&c, 2, 2, x, y, original, algo);

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
        sprintf(filepath, "%s%s", path, file->d_name);
        remove(filepath);
    }
    closedir(dir);
}

void stack_init(char* directory, int nbFiles) {
    pthread_cond_init(&stack.can_produce, NULL);
    pthread_cond_init(&stack.can_consume, NULL);
    pthread_mutex_init(&stack.lock, NULL);
    stack.max = STACK_MAX;
    stack.count = 0;
    stack.allFilenames[nbFiles];
    stack.nbFiles = nbFiles;
	stack.conversionAmount = 0;

	DIR *dir = opendir(directory);
	struct dirent *file;
	int n = 0;
	while (n < nbFiles && (file = readdir(dir)) != NULL) {
		char* extension = strrchr(file->d_name, '.');
		if (extension && !strcmp(extension, ".bmp")) {
			stack.allFilenames[n++] = file->d_name;
		}
	}
	closedir(dir);
}

void stack_destroy() {
	pthread_cond_destroy(&stack.can_consume);
	pthread_cond_destroy(&stack.can_produce);
	pthread_mutex_destroy(&stack.lock);
}

void* producer(void* args) {
	printf("Producer created\n");
	Producer_Arg* p_args = (Producer_Arg*) args;
	char* inputFolder = p_args->inputFolder;
	char* algo = p_args->algo;
	
	while (stack.conversionAmount < stack.nbFiles) {
		pthread_mutex_lock(&stack.lock);
		while(stack.count >= stack.max) {
			pthread_cond_wait(&stack.can_produce, &stack.lock);
		}
		if (stack.conversionAmount >= stack.nbFiles) {
			pthread_mutex_unlock(&stack.lock);
			break;
		}

		int current = stack.conversionAmount++;
		pthread_mutex_unlock(&stack.lock);

		char* filename = stack.allFilenames[current];
		char filepath[256];

		sprintf(filepath, "%s%s", inputFolder, filename);

		Image img = open_bitmap(filepath);
		Image_File new_bmp;
		new_bmp.filename = malloc(strlen(filename) + 1);
		strcpy(new_bmp.filename, filename);

		apply_effect(&img, &new_bmp.image, algo);
		printf("Applied effect on file: %s\n", filepath);

		pthread_mutex_lock(&stack.lock);
		stack.data[stack.count++] = new_bmp;
		pthread_cond_signal(&stack.can_consume);
		pthread_mutex_unlock(&stack.lock);
	}
	printf("Producer finished\n");
}

void* consumer(void* arg) {
	printf("Consumer created\n");
	int index, processed = 0;
	char* outputFolder = (char*) arg;

	while(processed < stack.nbFiles) {
		pthread_mutex_lock(&stack.lock);
		while (stack.count < 1) {
			pthread_cond_wait(&stack.can_consume, &stack.lock);
		}
		index = stack.count-1;
		Image_File file = stack.data[index];
		//pthread_mutex_unlock(&stack.lock); 

		char filepath[256];

		sprintf(filepath, "%s%s", outputFolder, file.filename);
		save_bitmap(file.image, filepath);
		processed++;
		printf("\nOutput file: %s. Total: %d\n", filepath, processed);

		//pthread_mutex_lock(&stack.lock);
		stack.count--;
		pthread_cond_signal(&stack.can_produce);
		pthread_mutex_unlock(&stack.lock);
	}

	sleep(1);
	printf("Consumer finished\n");
}

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
    //printf("Images to process: %d\n", nbImages);  // Bug mémoire si décommenté
    
    // Output folder
    char* outputFolder = argv[2];
    emptyDir(outputFolder);

    // Number of threads
    int threadCount = atoi(argv[3]);
    if (threadCount <= 0 || threadCount > nbImages) {
        printf("Invalid number of threads: %d\n", threadCount);
        return 0;
    }
    //printf("Number of threads: %d\n", threadCount);  // Bug mémoire si décommenté

    // Algorithm
    char* algo = argv[4];
    if (strcmp(algo, "boxblur") && strcmp(algo, "edgedetect") && strcmp(algo, "sharpen")) {
        printf("Invalid algorithm. Please use one of the followings: boxblur, edgedetect, sharpen\n");
        return 0;
    }

    stack_init(inputFolder, nbImages);

	pthread_t threads[threadCount + 1];
	pthread_attr_t attr;
	pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	printf("After Files:\n");
	for (int i = 0; i < stack.nbFiles; i++) {
		printf("%d: %s\n", i, stack.allFilenames[i]);
	}

	Producer_Arg args = {inputFolder, algo};
    for(int i = 0; i < threadCount; i++) {
		pthread_create(&threads[i], &attr, producer, (void*) &args);
	}

	pthread_create(&threads[threadCount], NULL, consumer, (void*) outputFolder);
	pthread_join(threads[threadCount], NULL);

	stack_destroy();
	pthread_attr_destroy(&attr);

	printf("\nEnd Files:\n");
	for (int i = 0; i < stack.nbFiles; i++) {
		printf("%d: %s\n", i, stack.allFilenames[i]);
	}

	return 0;
}