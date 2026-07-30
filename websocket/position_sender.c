#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>


int connect_to_server(const char *ip, int port){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}
// socket() + connect(), same pattern as your original chat client

void send_position(int sockfd, int x, int y){
    // pack x,y into a message and send() — decide text ("123,45\n") or binary
    char message[50];
    snprintf(message, sizeof(message), "%d,%d\n", x, y);
    send(sockfd, message, strlen(message), 0);
}

int main() {
    const char *server_ip = "192.168.7.2";
    int server_port = 8080;
    int sockfd = connect_to_server(server_ip, server_port);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to connect to server\n");
        return -1;
    }

    // Example usage: send position (100, 200)
    send_position(sockfd, 100, 200);

    close(sockfd);
    return 0;
}