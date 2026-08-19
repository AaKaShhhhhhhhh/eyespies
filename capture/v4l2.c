#include<stdio.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/ioctl.h>
#include<linux/videodev2.h>
#include<string.h>
#include<stdlib.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <errno.h>
#include "control/control_loop.h"
#include "pmw/pmw_servo.h"
#include "detection/color_threshold.h"   /* Position + find_target_position() */



int open_device(const char *dev_path) {
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("FAILED TO OPEN CAMERA");
    }
    return fd;
}

/* ---------------------------------------------------------------------------
 * UVC cameras (especially cheap ones) expose MULTIPLE /dev/video* nodes:
 *   - one real VIDEO_CAPTURE node (the actual frames)
 *   - often a second META_CAPTURE / metadata node
 * The numbering (video0 vs video1) is NOT stable across reboots/replugs.
 * So we validate a node with QUERYCAP and, if the preferred one is wrong,
 * scan /dev/video0..video9 for the first genuine capture+streaming node.
 * ------------------------------------------------------------------------- */
static int is_video_capture_node(int fd) {
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
        return 0;
    unsigned needed = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
    return (cap.capabilities & needed) == needed;
}

/* Opens the best capture device.
 *   - If `preferred` is given AND is a real capture node, use it.
 *   - Otherwise scan /dev/video0..video9 and pick the first valid one.
 * Returns an open fd, or -1 if none found. */
int find_capture_device(const char *preferred) {
    if (preferred) {
        int fd = open(preferred, O_RDWR);
        if (fd >= 0) {
            if (is_video_capture_node(fd)) {
                printf("Using preferred capture device: %s\n", preferred);
                return fd;
            }
            printf("Note: %s is NOT a video-capture node (maybe metadata) — scanning...\n",
                   preferred);
            close(fd);
        } else {
            perror("open(preferred)");
        }
    }
    for (int i = 0; i < 10; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/video%d", i);
        int fd = open(path, O_RDWR);
        if (fd < 0) continue;
        if (is_video_capture_node(fd)) {
            printf("Auto-detected capture device: %s\n", path);
            return fd;
        }
        close(fd);
        /* path exists but isn't capture (e.g. metadata) — keep scanning */
    }
    fprintf(stderr, "ERROR: no V4L2 video-capture device found under /dev/video*\n");
    return -1;
}

void query_capabilities(int fd) { 

    struct v4l2_capability cap;
    if(ioctl(fd ,VIDIOC_QUERYCAP , &cap) < 0) {
        perror("FAILED TO QUERY CAPABILITIES");
    } else {
        printf("Driver: %s\n", cap.driver);
        printf("Card: %s\n", cap.card);
        printf("Bus Info: %s\n", cap.bus_info);
        printf("Version: %u.%u.%u\n", (cap.version >> 16) & 0xFF, (cap.version >> 8) & 0xFF, cap.version & 0xFF);
    }

}
void set_format(int fd , int width , int height) {

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if(ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("FAILED TO SET FORMAT");
    }
}

int request_buffers(int fd , int count) { 

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if(ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("FAILED TO REQUEST BUFFERS");
        return -1;
    }
    return req.count;
}

void* buffer_addresses[4]; // Array to hold buffer addresses
size_t buffer_sizes[4]; // Array to hold buffer sizes

void map_buffers(int fd , int index) {

    // Querying buffer information
    struct v4l2_buffer buff;
    memset(&buff , 0 , sizeof(buff));
    buff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buff.memory = V4L2_MEMORY_MMAP;
    buff.index = index;

    if(ioctl(fd , VIDIOC_QUERYBUF , &buff) < 0) {
        perror("FAILED TO QUERY BUFFER");
        return;
    }

    // extracting buffer size and offset
    size_t buffer_size = buff.length;
    off_t offset = (off_t)buff.m.offset;

    printf("Buffer %d: size=%zu, offset=%ld\n", buff.index, buffer_size, offset);

    // mapping the buffer to user space
    void *mapped_buffer = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);

    buffer_addresses[buff.index] = mapped_buffer;
    buffer_sizes[buff.index] = buffer_size;
    if (mapped_buffer == MAP_FAILED) {
        perror("FAILED TO MAP BUFFER");
        return;
    }

    printf("Buffer %d mapped at address %p\n", buff.index, mapped_buffer);

    // After mapping, queue the buffer
    

}

void queue_all_buffers(int fd , int buffers) {

    for(int i =0  ; i < buffers ; i++) {
        
        struct v4l2_buffer buff;
        memset(&buff , 0 , sizeof(buff));
        buff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buff.memory = V4L2_MEMORY_MMAP;
        buff.index = i;

        if(ioctl(fd , VIDIOC_QBUF , &buff) < 0) {
            perror("FAILED TO QUEUE BUFFER");
        }
    }
}

void start_streaming(int fd) {

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd , VIDIOC_STREAMON , &type) < 0) {
        perror("FAILED TO START STREAMING");    
    }
}
void save_to_file(const void *buffer, size_t size) {
    
    FILE *file = fopen("frame.yuv", "wb");
    if (file) {
        fwrite(buffer, 1, size, file);
        fclose(file);
        printf("Saved frame to frame.yuv\n");
    } else {
        perror("FAILED TO OPEN FILE FOR WRITING");
    }
}

void capture_loop(int fd, int buffer_count, int width, int height , AxisState *pan , AxisState *tilt , const char *pan_pwm_path , const char *tilt_pwm_path) {
    
    const float pan_gain = 0.05f;
    const float tilt_gain = 0.05f;
    const float smoothning = 0.7f;
    const float deadband  = 1.5f;
    int consecutive_errors = 0;

    while(1) {
        // Wait (with timeout) for a frame instead of busy-spinning.
        // This keeps the CPU free so the servo thread stays smooth
        // even if the camera stalls.
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec  = 2;
        tv.tv_usec = 0;

        int r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r == 0) {
            printf("WARNING: camera timeout (no frame in 2s)\n");
            usleep(100000);
            continue;
        } else if (r < 0) {
            if (errno == EINTR) continue;
            perror("select() failed");
            break;
        }

        struct v4l2_buffer buff;
        memset(&buff , 0 , sizeof(buff));
        buff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buff.memory = V4L2_MEMORY_MMAP;

        // Dequeue a buffer
        if(ioctl(fd , VIDIOC_DQBUF , &buff) < 0) {
            if (errno == ENODEV) {
                perror("CAMERA DISCONNECTED (ENODEV)");
                break;   // device is gone — stop cleanly instead of spinning
            }
            perror("FAILED TO DEQUEUE BUFFER");
            consecutive_errors++;
            if (consecutive_errors > 50) {
                printf("Too many dequeue errors, stopping capture.\n");
                break;
            }
            usleep(10000);
            continue;
        }
        consecutive_errors = 0;

        // Process the captured frame (for example, save it to a file)
        // printf("Captured frame in buffer %d\n", buff.index);
        
        Position pos = find_target_position(buffer_addresses[buff.index], width, height);

        if(pos.found) {
            printf("Target color found at position: (%d, %d)\n", pos.x,pos.y);

            float pan_delta = error_to_angle_delta(pos.x , width/2 , pan_gain);
            float pan_raw = pan->current_angle+pan_delta;
            float pan_new = clamp_angle(
                smooth_angle(pan->current_angle , pan_raw , smoothning) , 0 , 180);
            if(should_update(pan->current_angle , pan_new , deadband)) {
                servo_set_angle(pan_pwm_path , pan_new);
                pan->current_angle = pan_new;
            }

            float tilt_delta = error_to_angle_delta(pos.y , height/2 , tilt_gain);
            float tilt_raw = tilt->current_angle+tilt_delta;
            float tilt_new = clamp_angle(
                smooth_angle(tilt->current_angle , tilt_raw , smoothning) , 0 , 180);
            if(should_update(tilt->current_angle , tilt_new , deadband)) {
                servo_set_angle(tilt_pwm_path , tilt_new);
                tilt->current_angle = tilt_new;
            }
        }

        // Re-queue the buffer
        if(ioctl(fd , VIDIOC_QBUF , &buff) < 0) {
            perror("FAILED TO RE-QUEUE BUFFER");
        }
    }
}
void stop_streaming(int fd) {
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(fd , VIDIOC_STREAMOFF , &type) < 0) {
        perror("FAILED TO STOP STREAMING");
    }
}

void cleanup ( int fd , int buffer_count) {
    
    stop_streaming(fd);

    for(int i = 0; i < buffer_count; i++) {
        struct v4l2_buffer buff;
        memset(&buff , 0 , sizeof(buff));
        buff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buff.memory = V4L2_MEMORY_MMAP;
        buff.index = i;

        // Unmap the buffer
        if(munmap(buffer_addresses[buff.index], buffer_sizes[buff.index]) < 0) {
            perror("FAILED TO UNMAP BUFFER");
        }
    }

    close(fd);
}

