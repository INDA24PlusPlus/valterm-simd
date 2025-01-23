#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <png.h>
#include <immintrin.h>
#include <x86intrin.h>

#define WIDTH 8192
#define HEIGHT 8192
#define MAX_ITERATIONS 200
#define X_SCALE 4.0
#define Y_SCALE 4.0

#define CENTER_X(x) ((x) - WIDTH/2)
#define CENTER_Y(y) ((y) - HEIGHT/2)

__m256i mandelbrot_simd(__m256 x, __m256 y) {
    __m256 cx = _mm256_sub_ps(x, _mm256_set1_ps(WIDTH / 2));
    __m256 cy = _mm256_sub_ps(y, _mm256_set1_ps(HEIGHT / 2));
    cx = _mm256_mul_ps(cx, _mm256_set1_ps(1 / (WIDTH / X_SCALE)));
    cy = _mm256_mul_ps(cy, _mm256_set1_ps(1 / (HEIGHT / Y_SCALE)));

    __m256 zx = _mm256_setzero_ps();
    __m256 zy = _mm256_setzero_ps();
    __m256i iterations = _mm256_setzero_si256();
    __m256i max_iterations = _mm256_set1_epi32(MAX_ITERATIONS);

    __m256i one = _mm256_set1_epi32(1);

    for (int i = 0; i < MAX_ITERATIONS; i++) {
        __m256 zx2 = _mm256_mul_ps(zx, zx);
        __m256 zy2 = _mm256_mul_ps(zy, zy);
        __m256i mask = _mm256_castps_si256(_mm256_cmp_ps(_mm256_add_ps(zx2, zy2), _mm256_set1_ps(4), _CMP_LT_OQ));
        iterations = _mm256_add_epi32(iterations, _mm256_and_si256(mask, one));
        if (_mm256_testz_si256(mask, mask)) {
            break;
        }
        __m256 temp = _mm256_add_ps(_mm256_sub_ps(zx2, zy2), cx);
        zy = _mm256_add_ps(_mm256_mul_ps(_mm256_mul_ps(_mm256_set1_ps(2), zx), zy), cy);
        zx = temp;
    }

    // Ugly and can be done easier but loses precision, so we'll go with this way to get equivalent results
    __m256i c = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_div_ps(_mm256_cvtepi32_ps(iterations), _mm256_cvtepi32_ps(max_iterations)), _mm256_set1_ps(255.0f)));

    return c;
}

void mandelbrot(png_bytep px, int x, int y) {
    float cx = CENTER_X(x) / (WIDTH / X_SCALE);
    float cy = CENTER_Y(y) / (HEIGHT / Y_SCALE);
    float zx = 0, zy = 0;
    int iterations = 0;

    while (zx * zx + zy * zy < 4 && iterations < MAX_ITERATIONS) {
        float temp = zx * zx - zy * zy + cx;
        zy = 2 * zx * zy + cy;
        zx = temp;
        iterations++;
    }

    int c = (int)((iterations / (float)MAX_ITERATIONS) * 255);
    px[0] = c;
    px[1] = c;
    px[2] = c;
}

void render(png_structp png, png_infop info, png_bytepp row_pointers) {
    for(int y = 0; y < HEIGHT; y++) {
        row_pointers[y] = (png_bytep)malloc(png_get_rowbytes(png, info));
        for(int x = 0; x < WIDTH; x++) {
            png_bytep px = &(row_pointers[y][x * 3]);
            mandelbrot(px, x, y);
        }
    }
}

void render_simd(png_structp png, png_infop info, png_bytepp row_pointers) {
    __m256 xx = _mm256_setr_ps(0, 1, 2, 3, 4, 5, 6, 7);
    for(int y = 0; y < HEIGHT; y++) {
        row_pointers[y] = (png_bytep)malloc(png_get_rowbytes(png, info));
        for(int x = 0; x < WIDTH; x+=8) {
            png_bytep px = &(row_pointers[y][x * 3]);
            __m256i c = mandelbrot_simd(_mm256_add_ps(_mm256_set1_ps(x), xx), _mm256_set1_ps(y));
            int c0[8];
            _mm256_storeu_si256((__m256i*)c0, c);

            for (int i = 0; i < 8; i++) {
                px[i*3] = c0[i];
                px[i*3+1] = c0[i];
                px[i*3+2] = c0[i];
            }
        }
    }
}

int main(void) {
    png_byte bit_depth = 8;
    png_byte color_type = PNG_COLOR_TYPE_RGB;

    FILE *fp = fopen("out.png", "wb");
    if(!fp) {
        fprintf(stderr, "Could not open file for writing\n");
        return 1;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fprintf(stderr, "Could not allocate write struct\n");
        return 1;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        fprintf(stderr, "Could not allocate info struct\n");
        return 1;
    }

    png_init_io(png, fp);

    png_set_IHDR(
        png,
        info,
        WIDTH, HEIGHT,
        bit_depth,
        color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );
    png_write_info(png, info);

    png_bytepp row_pointers = (png_bytepp)malloc(sizeof(png_bytep) * HEIGHT);
    render_simd(png, info, row_pointers);
    
    png_write_image(png, row_pointers);

    png_write_end(png, NULL);

    fclose(fp);
    png_free_data(png, info, PNG_FREE_ALL, -1);
    png_destroy_write_struct(&png, &info);
    free(row_pointers);

    return 0;
}