#include<stdio.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/ioctl.h>
#include<linux/videodev2.h>
#include<string.h>
#include<stdlib.h>
#include<sys/mman.h>



int open_device(const char *dev_path) {
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("FAILED TO OPEN CAMERA");
    }
    return fd;
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
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

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

void capture_loop(int fd , int buffer_count) {
    
    while(1) {
        struct v4l2_buffer buff;
        memset(&buff , 0 , sizeof(buff));
        buff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buff.memory = V4L2_MEMORY_MMAP;

        // Dequeue a buffer
        if(ioctl(fd , VIDIOC_DQBUF , &buff) < 0) {
            perror("FAILED TO DEQUEUE BUFFER");
            continue;
        }

        // Process the captured frame (for example, save it to a file)
        printf("Captured frame in buffer %d\n", buff.index);

        // Re-queue the buffer
        if(ioctl(fd , VIDIOC_QBUF , &buff) < 0) {
            perror("FAILED TO RE-QUEUE BUFFER");
        }
    }
}


int main()  { 

   int fd = open_device("/dev/video0");
   if(fd < 0 ) {
    perror("FAILED TO OPEN DEVICE");
   }

    query_capabilities(fd);

    set_format(fd, 640, 480);

    int buffer_count = request_buffers(fd, 4);

    if(buffer_count < 0) {
        perror("FAILED TO REQUEST BUFFERS");
    }
    
     for(int i = 0; i < buffer_count; i++) {
         map_buffers(fd, i);
     }
    queue_all_buffers(fd, buffer_count);
    start_streaming(fd);
    capture_loop(fd, buffer_count);
    return 0;

    
}