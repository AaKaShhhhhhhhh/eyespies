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

    unsigned char *frame = malloc(width * height * 3 / 2);
    if(!frame) {                                                
        perror("malloc");
        close(fd);
        return NULL;
    }

    ssize_t bytes_read = read(fd, frame, width * height * 3 / 2);
    if(bytes_read < 0) {
        perror("read");
        free(frame);
        close(fd);
        return NULL;
    }

    close(fd);
    return frame;
}

void get_pixel_yuv(unsigned char *frame, int height, int width, int x, int y, unsigned char *out_y, unsigned char *out_u, unsigned char *out_v) {
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
    // Define the target color range in YUV space
    unsigned char target_y_min = 16;  // Example minimum Y value
    unsigned char target_y_max = 235; // Example maximum Y value
    unsigned char target_u_min = 80;  // Example minimum U value
    unsigned char target_u_max = 130; // Example maximum U value
    unsigned char target_v_min = 90;  // Example minimum V value
    unsigned char target_v_max = 140; // Example maximum V value

    return (y >= target_y_min && y <= target_y_max) &&
           (u >= target_u_min && u <= target_u_max) &&
           (v >= target_v_min && v <= target_v_max);
}

Position find_target_position(unsigned char *frame , int width , int height) {
    Position pos = {0, 0, 0};
    unsigned char y, u, v;

    for(int y = 0; y < height; y++) {
        for(int x = 0; x < width; x++) {
            get_pixel_yuv(frame, height, width, x, y, &y, &u, &v);
            if(is_target_color(y, u, v)) {
                pos.x = x;
                pos.y = y;
                pos.found = 1;
                return pos;
            }
        }
    }

    pos.found = 0;
    return pos;

}

void mark_postion_on_frame(unsigned char *frame , int width , int height , Position pos) {
    if(!pos.found) return;

    // Example: Mark the found position with a simple color
    unsigned char mark_y = 255;
    unsigned char mark_u = 0;
    unsigned char mark_v = 0;

    int mark_size = 5; // Size of the mark
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