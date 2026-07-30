#include<stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include<unistd.h>

typedef struct {
    int x ;
    int y ;
    int found;
} Position;

unsigned char* load_yuv_frame(const char *path , int width , int height) {
    int fd = open(path , O_RDONLY);
    if(fd < 0) {
        perror("open");
        return NULL;
    }

    size_t expected = (size_t)width * (size_t)height * 3 / 2;
    unsigned char *frame = malloc(expected);
    if(!frame) {
        perror("malloc");
        close(fd);
        return NULL;
    }

    ssize_t bytes_read = read(fd, frame, expected);
    if(bytes_read < 0) {
        perror("read");
        free(frame);
        close(fd);
        return NULL;
    }
    if((size_t)bytes_read != expected) {
        fprintf(stderr,
                "short read: expected %zu bytes, got %zd bytes\n",
                expected, bytes_read);
        free(frame);
        close(fd);
        return NULL;
    }

    close(fd);
    return frame;
}

void get_pixel_yuv(unsigned char *frame, int height, int width, int x, int y,
                   unsigned char *out_y, unsigned char *out_u, unsigned char *out_v) {
    if(x < 0 || x >= width || y < 0 || y >= height) {
        fprintf(stderr, "Coordinates out of bounds\n");
        return;
    }

    int y_index = y * width + x;
    *out_y = frame[y_index];

    int uv_index = (y / 2) * (width / 2) + (x / 2);
    *out_u = frame[width * height + uv_index];
    *out_v = frame[width * height + (width / 2) * (height / 2) + uv_index];
}

int is_target_color(unsigned char y , unsigned char u , unsigned char v) {
    unsigned char target_y_min = 16;
    unsigned char target_y_max = 235;
    unsigned char target_u_min = 80;
    unsigned char target_u_max = 130;
    unsigned char target_v_min = 90;
    unsigned char target_v_max = 140;

    return (y >= target_y_min && y <= target_y_max) &&
           (u >= target_u_min && u <= target_u_max) &&
           (v >= target_v_min && v <= target_v_max);
}

Position find_target_position(unsigned char *frame , int width , int height) {
    Position pos = {0, 0, 0};

    long sum_x = 0;
    long sum_y = 0;
    long count = 0;

    for(int yy = 0; yy < height; yy++) {
        for(int xx = 0; xx < width; xx++) {
            unsigned char py, u, v;
            get_pixel_yuv(frame, height, width, xx, yy, &py, &u, &v);
            if(is_target_color(py, u, v)) {
                sum_x += xx;
                sum_y += yy;
                count++;
            }
        }
    }

    if(count > 0) {
        pos.x = (int)(sum_x / count);
        pos.y = (int)(sum_y / count);
        pos.found = 1;
    }

    return pos;
}

void mark_postion_on_frame(unsigned char *frame , int width , int height , Position pos) {
    if(!pos.found) return;

    unsigned char mark_y = 255;
    unsigned char mark_u = 0;
    unsigned char mark_v = 0;

    int mark_size = 5;
    for(int dy = -mark_size; dy <= mark_size; dy++) {
        for(int dx = -mark_size; dx <= mark_size; dx++) {
            int mark_x = pos.x + dx;
            int mark_y_pos = pos.y + dy;
            if(mark_x >= 0 && mark_x < width && mark_y_pos >= 0 && mark_y_pos < height) {
                int y_index = mark_y_pos * width + mark_x;
                frame[y_index] = mark_y;

                int uv_index = (mark_y_pos / 2) * (width / 2) + (mark_x / 2);
                frame[width * height + uv_index] = mark_u;
                frame[width * height + (width / 2) * (height / 2) + uv_index] = mark_v;
            }
        }
    }
}

int main() {
    const char *path = "frame.yuv";
    int width = 640;
    int height = 480;

    unsigned char *frame = load_yuv_frame(path, width, height);
    if(!frame) {
        return EXIT_FAILURE;
    }

    Position pos = find_target_position(frame, width, height);
    if(pos.found) {
        printf("Target color found at position: (%d, %d)\n", pos.x, pos.y);
        mark_postion_on_frame(frame, width, height, pos);
    } else {
        printf("Target color not found in the frame.\n");
    }

    free(frame);
    return EXIT_SUCCESS;
}
