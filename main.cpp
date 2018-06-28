#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <unistd.h>


#include <errno.h>
#include <signal.h>
#include <cstring>
#include <pthread.h>

#define BUFFSIZE 64
#define TIMEOUT_MS 3500
#define TIMEOUT_CTR 5
#define MSGPORT 50025
//using namespace std;

volatile sig_atomic_t work = 1;
void signalbreak(int sig){
    work=0;
    printf("\nBreak Signal Received.\n");
    exit(0);
}
struct threaddata{
    int sfd;
    struct sockaddr_in* msgaddr;
    int port;
    int portend;
    struct timeval tv;
};
int scanhandler(struct threaddata* td){
    int sfd=td->sfd;
    sockaddr_in* msgaddr=td->msgaddr;
    socklen_t msgaddrlen= sizeof(*msgaddr);
    int port=td->port;
    int portend=td->portend;
    struct timeval tv=td->tv;
    printf("Client Listening Thread Created from %d to %d.\n", port, portend);
    int fd;

    unsigned char buf[BUFFSIZE];
    unsigned char msgbuf[BUFFSIZE];
    while(port<=portend){
        //printf("Scanning Port %d.\n", port);
        memset(buf,0,BUFFSIZE);
        memset(msgbuf,0,BUFFSIZE);
        if((fd=socket(AF_INET, SOCK_DGRAM,0))<0) {
            perror("Unable to start UDP scan socket");
            return 0;
        }
        //set ip address
        struct sockaddr_in addr;
        struct sockaddr_in readdr;
        socklen_t readdrlen= sizeof(readdr);
        memset((char*)&addr, 0, sizeof(addr));
        addr.sin_family=AF_INET;
        addr.sin_addr.s_addr= htonl(INADDR_ANY);
        addr.sin_port=htons(port);
        if((bind(fd, (struct sockaddr*)&addr, sizeof(addr)))<0){
            printf("Unable to bind scan socket at port %d.\n", port);
            port++;
            close(fd);
            /*usleep(TIMEOUT_MS*1000);*/
            continue;
        }
        int recvlen=0, msgrecvlen=0;
        if((setsockopt(fd,SOL_SOCKET, SO_RCVTIMEO,&tv, sizeof(tv)))<0)
            perror("error setting recv timout");
        // send messages first;
        int msglen=sprintf((char *)msgbuf,"%d",port);
        if(sendto(sfd,msgbuf, msglen,0, NULL, 0)<0){
            //perror("TCP message link broken");
            printf("[ERR] TCP message link ended.\n");
            break;
        }

        recvlen=recvfrom(fd, buf, BUFFSIZE, 0, (struct sockaddr*)&readdr, &readdrlen);
        if(recvlen>0){
            //recvtotal+=recvlen;
            if((sendto(fd, buf, BUFFSIZE, 0, (struct sockaddr*)&readdr, readdrlen)<0)){
                printf("Port %d NOT active out.\n", port);
                ++port;
                close(fd);
                continue;
            }
            /*else
                printf("Port %d active.\n", port);*/
        }
        else if(recvlen==0){
            printf("Port %d NO MESSAGE.\n", port);
            ++port;
            close(fd);
            continue;
        }
        else if(recvlen<0){
            printf("Port %d ERROR %s.\n", port, strerror(errno));
            ++port;
            close(fd);
            //usleep(TIMEOUT_MS*1000/2);
            continue;
        }
        ++port;
        close(fd);
    }
    //char* endmsg="end";
    //sendto(sfd,endmsg, strlen(endmsg),0, NULL, 0);
    close(sfd);
    printf("Scan session ends.\n");
    delete td;
    return 0;
}
int main(int argc, char *argv[]) {

    int fd,sfd;
    signal(SIGINT,signalbreak);
    //time interval for scan
    struct timeval tv;
    tv.tv_sec=TIMEOUT_MS/1000;
    tv.tv_usec=1000*(TIMEOUT_MS-(TIMEOUT_MS/1000)*1000);

    struct timeval tvmsg;
    tvmsg.tv_sec=TIMEOUT_CTR;
    tvmsg.tv_usec=0;
    /*Set message IP*/
    struct sockaddr_in addr;
    socklen_t addrlen= sizeof(addr);
    //set socket
    memset((char*)&addr, 0, sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr= htonl(INADDR_ANY);
    addr.sin_port=htons(MSGPORT);
    int recvlen;
    int opt=1;
    unsigned char msgbuf[BUFFSIZE];

    if((fd=socket(AF_INET, SOCK_STREAM,0))<0) {
        perror("Unable to start TCP msg socket");
        return -1;
    }
    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr))<0){
        perror("Unable to bind msg socket");
        return -1;
    }
    if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR | SO_REUSEADDR,&opt, sizeof(opt))){
        perror("Error setting options for TCP socket");
        return -1;
    }

    int port, portend;
    if(listen(fd,3)<0){
        perror("Fail to listen to MSGPORT");
    }
    printf("Server started, waiting for clients:\n");
    while(work){
        if((sfd=accept(fd,(struct sockaddr*)&addr,&addrlen))<0){
            perror("Error Accepting the socket.\n");
            return -1;
        }
        recvlen=recvfrom(sfd, msgbuf, BUFFSIZE, 0, (struct sockaddr*)&addr, &addrlen);
        if(recvlen>0){
            //process request, process port number;
            char *pivot=strstr((char *)msgbuf,"request:");
            if(pivot==NULL||pivot!=(char*)msgbuf){
                //bad message
                memset(msgbuf,0,BUFFSIZE);
                continue;
            }
            else{
                //process port numbers
                pivot+=8;
                char* bar=strstr((char *)msgbuf,"-");
                if(bar==NULL){
                    //only 1 port
                    port=atoi(pivot);
                    portend=port;
                }
                else{
                    //range of ports
                    portend=atoi(bar+1);
                    memset(bar,0,(char*)msgbuf+BUFFSIZE-bar);
                    port=atoi(pivot);
                }
                memset(msgbuf,0,BUFFSIZE);
            }
            printf("Received Client Request\n");
            //send ack back
            char* ack="OK";
            if(sendto(sfd, ack, 2, 0, NULL, 0)<0)
            {
                perror("ACK for client not sent.\n");
                return -1;
            }
            printf("ACK sent.\n");
            struct threaddata* td=new struct threaddata;
            td->sfd=sfd;
            td->msgaddr=&addr;
            td->port=port;
            td->portend=portend;
            td->tv=tv;
            std::thread scanthread(scanhandler,td);
            scanthread.join();
            printf("Waiting for new clients:\n");
            //
        }
    }
    printf("Server Quit. Bye!!\n");
    return 0;
}
