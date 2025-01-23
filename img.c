#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <png.h>
#include <math.h>
#include <immintrin.h>

#include "ubench.h"

#define WIDTH 1024
#define HEIGHT 1024
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
    //__m256i c = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_div_ps(_mm256_cvtepi32_ps(iterations), _mm256_cvtepi32_ps(max_iterations)), _mm256_set1_ps(255.0f)));

    return iterations;
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

    int r = (int)(255 * sinf(0.1 * iterations));
    int g = (int)(255 * sinf(0.2 * iterations));
    int b = (int)(255 * sinf(0.3 * iterations));
    if (iterations == MAX_ITERATIONS) {
        r = g = b = 0;
    }
    px[0] = r;
    px[1] = g;
    px[2] = b;
}

void render(png_structp png, png_infop info, png_bytepp row_pointers) {
    for(int y = 0; y < HEIGHT; y++) {
        for(int x = 0; x < WIDTH; x++) {
            png_bytep px = &(row_pointers[y][x * 3]);
            mandelbrot(px, x, y);
        }
    }
}

void render_simd(png_structp png, png_infop info, png_bytepp row_pointers) {
    __m256 xx = _mm256_setr_ps(0, 1, 2, 3, 4, 5, 6, 7);
    for(int y = 0; y < HEIGHT; y++) {
        for(int x = 0; x < WIDTH; x+=8) {
            png_bytep px = &(row_pointers[y][x * 3]);
            __m256i iterations = mandelbrot_simd(_mm256_add_ps(_mm256_set1_ps(x), xx), _mm256_set1_ps(y));
            __m256 iterationsf = _mm256_cvtepi32_ps(iterations);
            __m256 scale = _mm256_set1_ps(255.f);
            __m256i rx = _mm256_cvttps_epi32(_mm256_mul_ps(_mm256_sin_ps(_mm256_mul_ps(iterationsf, _mm256_set1_ps(0.1f))), scale));
            __m256i gx = _mm256_cvttps_epi32(_mm256_mul_ps(_mm256_sin_ps(_mm256_mul_ps(iterationsf, _mm256_set1_ps(0.2f))), scale));
            __m256i bx = _mm256_cvttps_epi32(_mm256_mul_ps(_mm256_sin_ps(_mm256_mul_ps(iterationsf, _mm256_set1_ps(0.3f))), scale));
            __m256i mask = _mm256_cmpeq_epi32(iterations, _mm256_set1_epi32(MAX_ITERATIONS));
            rx = _mm256_blendv_epi8(rx, _mm256_set1_epi32(0), mask);
            gx = _mm256_blendv_epi8(gx, _mm256_set1_epi32(0), mask);
            bx = _mm256_blendv_epi8(bx, _mm256_set1_epi32(0), mask);

            int r[8], g[8], b[8];
            _mm256_storeu_si256((__m256i*)r, rx);
            _mm256_storeu_si256((__m256i*)g, gx);
            _mm256_storeu_si256((__m256i*)b, bx);

            for (int i = 0; i < 8; i++) {
                px[i * 3] = r[i];
                px[i * 3 + 1] = g[i];
                px[i * 3 + 2] = b[i];
            }
        }
    }
}

int setup_png(FILE** fp, png_structp* png, png_infop* info, png_bytepp* row_pointers) {
    png_byte bit_depth = 8;
    png_byte color_type = PNG_COLOR_TYPE_RGB;

    *fp = fopen("out.png", "wb");
    if(!*fp) {
        fprintf(stderr, "Could not open file for writing\n");
        return 1;
    }

    *png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!*png) {
        fprintf(stderr, "Could not allocate write struct\n");
        return 1;
    }

    *info = png_create_info_struct(*png);
    if (!*info) {
        fprintf(stderr, "Could not allocate info struct\n");
        return 1;
    }

    png_init_io(*png, *fp);

    png_set_IHDR(
        *png,
        *info,
        WIDTH, HEIGHT,
        bit_depth,
        color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );
    png_write_info(*png, *info);

    *row_pointers = (png_bytepp)malloc(sizeof(png_bytep) * HEIGHT);
    for (int y = 0; y < HEIGHT; y++) {
        (*row_pointers)[y] = (png_bytep)malloc(png_get_rowbytes(*png, *info));
    }

    return 0;
}

int destroy_png_nowrite(FILE* fp, png_structp png, png_infop info, png_bytepp row_pointers) {
    fclose(fp);
    png_free_data(png, info, PNG_FREE_ALL, -1);
    png_destroy_write_struct(&png, &info);
    free(row_pointers);
    return 0;
}

int destroy_png_write(FILE* fp, png_structp png, png_infop info, png_bytepp row_pointers) {
    png_write_image(png, row_pointers);
    png_write_end(png, NULL);
    return destroy_png_nowrite(fp, png, info, row_pointers);
}

#ifdef BENCH

UBENCH_EX(mandelbrot, sisd) {
    FILE* fp;
    png_structp png;
    png_infop info;
    png_bytepp row_pointers;

    if (setup_png(&fp, &png, &info, &row_pointers)) {
        return;
    }

    UBENCH_DO_BENCHMARK() {
        render(png, info, row_pointers);
    }
    
    if (destroy_png_nowrite(fp, png, info, row_pointers)) {
        return;
    }
}

UBENCH_EX(mandelbrot, simd) {
    FILE* fp;
    png_structp png;
    png_infop info;
    png_bytepp row_pointers;

    if (setup_png(&fp, &png, &info, &row_pointers)) {
        return;
    }

    UBENCH_DO_BENCHMARK() {
        render_simd(png, info, row_pointers);
    }
    
    if (destroy_png_nowrite(fp, png, info, row_pointers)) {
        return;
    }
}

UBENCH_MAIN();

#else

int main(void) {
    FILE* fp;
    png_structp png;
    png_infop info;
    png_bytepp row_pointers;

    if (setup_png(&fp, &png, &info, &row_pointers)) {
        return 1;
    }

    render_simd(png, info, row_pointers);
    
    if (destroy_png_write(fp, png, info, row_pointers)) {
        return 1;
    }

    return 0;
}
#endif