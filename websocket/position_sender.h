#ifndef POSITION_SENDER_H
#define POSITION_SENDER_H

int connect_to_server(const char *ip, int port);
void send_position(int sockfd, int x, int y);

#endif /* POSITION_SENDER_H */