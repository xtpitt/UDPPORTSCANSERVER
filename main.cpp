#include <iostream>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <unistd.h>


#include <errno.h>
#include <signal.h>
#include <cstring>
#include <pthread.h>

#define BUFFSIZE 64
#define PKTBUFSIZE 1000
#define TIMEOUT_MS 3500
#define WAVEQUOTE 600
#define TIMEOUT_CTR 5
#define MSGPORT 50025
#define SPTPORT 50027
#define DYMPORTL 60000
#define DYMPORTR 65535
#define PORTALLO 10
#define DROPTHRESHOLD 0.005
#define INTAJDTHRESHOLD 8
#define DROPTHRURGENT 0.01
#define BASEINTERVAL 80
#define DEFINTERVAL 550
#define SPTTIMEOUT 80



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
struct pshdata{
    int fd;
    struct sockaddr_in addr;
    socklen_t addrlen;
    struct timeval tv;
};
struct sptdata{
    int fd;
    struct sockaddr_in* addr;
    socklen_t addrlen;
};
void randpayloadset(char* payload, size_t len){
    srand(time(NULL));
    for(int i=0; i<len;++i){
        *(payload+i)=(char)(rand()%256);
    }
}
int speedtestsend_s(int udpfd, sockaddr_in* addr, int streamfd, char* msg, int dlto){
    char ubuf[PKTBUFSIZE];
    randpayloadset((char*)ubuf, PKTBUFSIZE);
    auto start = std::chrono::system_clock::now();
    auto end=std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end-start;
    std::chrono::duration<double> diff2;
    socklen_t socklen=sizeof(*addr);
    uint64_t count=0;
    uint64_t loss=0;
    double lossbalance=0;
    int intadjcount=0;
    int waveloss=0;
    double sumtime=0;
    int temp=WAVEQUOTE;
    int quote;
    int feedback;
    int sentl=0;
    double rate=0;
    double roundtime=0;
    double timeline;
    unsigned int waveno=0;
    std::string proceedstr="proceed";
    std::string testu="testu:";
    std::string endstr="end";
    double adaptivesleep=DEFINTERVAL;
    int countzeros=0;
    printf("Entering download testing cycle.\n");
    if(dlto<SPTTIMEOUT)
        dlto=SPTTIMEOUT;
    while(diff.count()<dlto){//default timeout 180s
        quote=temp;
        auto tick1=std::chrono::system_clock::now();
        memcpy(ubuf,&waveno, sizeof(int));
        while(quote>0){
            sentl=sendto(udpfd,ubuf,PKTBUFSIZE,0,(sockaddr*)addr, socklen);
            if(sentl<=0) {
                perror("Uplink speed testing udp broken");
            }
            else{
                count++;
                quote--;
            }
            //usleep(adaptivesleep);
            std::this_thread::sleep_for(std::chrono::microseconds((int)adaptivesleep));
        }
        waveno++;
        auto tick2=std::chrono::system_clock::now();
        diff2 = tick2-tick1;
        sumtime+=diff2.count();
        roundtime=diff2.count();
        end=std::chrono::system_clock::now();
        diff=end-start;
        timeline=diff.count();
        //send current sent quote to tcp socket
        memset(msg,0,BUFFSIZE);
        memcpy(msg,testu.c_str(),strlen(testu.c_str()));
        memcpy(msg+strlen(testu.c_str()),&timeline, sizeof(timeline));
        memcpy(msg+strlen(testu.c_str())+sizeof(sumtime),&roundtime, sizeof(roundtime));
        if((sendto(streamfd, msg, strlen(testu.c_str())+ sizeof(sumtime)+sizeof(roundtime), 0, NULL, 0))<0){
            perror("Message sending error: upload continue message");
            close(streamfd);
            close(udpfd);
            return -1;
        }
        memset(msg,0,BUFFSIZE);
        if((recvfrom(streamfd, msg, sizeof(feedback),0,NULL, NULL))<0){
            perror("NO FEEDBACK RECEIVED");
            close(streamfd);
            close(udpfd);
            return -1;
        }
        //get result;
        memcpy(&feedback,msg, sizeof(feedback));
        //compute loss;
        waveloss=temp-feedback;
        loss+=waveloss;
        /*if(feedback>temp){
            printf("Some packet out of sequence\n");
            adaptivesleep=adaptivesleep*1.1;
            countzeros=0;
        }*/
        if(waveloss>=0){
            lossbalance=lossbalance+(waveloss-lossbalance)/4;
        }
        //adjust timer
        if(lossbalance>DROPTHRURGENT*temp && waveloss>DROPTHRURGENT*temp&& intadjcount<INTAJDTHRESHOLD){
            adaptivesleep*=2;
            intadjcount+=2;
        }
        else if(lossbalance>DROPTHRESHOLD*temp && waveloss>DROPTHRESHOLD*temp&& intadjcount<INTAJDTHRESHOLD){
            adaptivesleep*=1.4;
            intadjcount++;
        }
        if(waveloss==0){
            countzeros++;
            if(intadjcount>0)
                intadjcount--;
            if(countzeros>=3){
                if(adaptivesleep>BASEINTERVAL*1.1)
                    adaptivesleep=adaptivesleep/1.1;
                else
                    adaptivesleep=BASEINTERVAL;
                countzeros=0;
            }

        } else
        countzeros=0;
        end=std::chrono::system_clock::now();
        diff=end-start;

        //printf("%f\n", diff.count());
    }
    printf("Upload speed test cycle finished\n");
    memset(msg,0,BUFFSIZE);

    //changed ending condition;
    rate=count*8/sumtime;
    char output[BUFFSIZE];
    sprintf((char*)output,"Upload Rate: %f kbps.\n", rate);
    printf(output);
    double lossrate=(double)loss/count;
    sprintf((char*)output,"Upload packet loss rate %f.\n", lossrate);
    printf(output);
    memset(msg, 0, BUFFSIZE);
    memcpy(msg, &rate, sizeof(rate));
    memcpy(msg+ sizeof(rate), &lossrate, sizeof(lossrate));
    if((sendto(streamfd, msg, sizeof(rate)+ sizeof(lossrate), 0, NULL, 0))<0){
        perror("Message sending error: speedtest result not sent.\n");
        close(streamfd);
        close(udpfd);
        return -1;
    }
}
int speedtestrecv_s(int udpfd, sockaddr_in* udpaddr, int sfd, char* msgbuf, struct timeval tv0, int& dlto){
    int readlen=0, count=0;
    char buf[PKTBUFSIZE];
    fd_set udpreadset, tcpreadset;
    int ures=0,tres=0;
    socklen_t udpaddrlen= sizeof(*udpaddr);
    auto tickcount=std::chrono::system_clock::now();
    auto ticknow=std::chrono::system_clock::now();
    std::chrono::duration<double> timeout;
    unsigned int waveno=0;
    unsigned int wavenorecv=0;
    unsigned int wrongwavecount=0;
    while(1){
        count=0;
        do {
            memset(buf, 0, PKTBUFSIZE);
            FD_ZERO(&udpreadset);
            FD_SET(udpfd, &udpreadset);
            ures=select(udpfd + 1, &udpreadset, NULL, NULL, &tv0);
            if(ures<0){
                perror("udpsocket");
            }
            else if(ures>0){
                if(FD_ISSET(udpfd, &udpreadset)){
                    readlen=recvfrom(udpfd, buf, PKTBUFSIZE, 0, (sockaddr*)udpaddr, &udpaddrlen);
                    memcpy(&wavenorecv,buf, sizeof(int));
                    if(readlen==PKTBUFSIZE&&wavenorecv==waveno){
                        count++;
                    }
                    else if(wavenorecv!=waveno){
                        wrongwavecount++;
                    }
                    else if(readlen>0){
                        printf("%d Partial reception???\n", readlen);
                    }

                }
            }
            //select works not well
            FD_ZERO(&tcpreadset);
            FD_SET(sfd, &tcpreadset);
            tres = select(sfd + 1, &tcpreadset, NULL, NULL, &tv0);
            if(tres<0||errno==EPIPE){
                perror("TCP control link broken");
                close(sfd);
                close(udpfd);
                return -1;
            }
            //timeout for broken pipe, happens when both reads zero
        } while (tres == 0);
        //process http packet if
        waveno++;
        memset(msgbuf,0 ,BUFFSIZE);
        //FIXME: We didn't check the FD_ISSET as only 1 socket is detected.
        int l=recvfrom(sfd, msgbuf, BUFFSIZE, 0, NULL, NULL);
        if(l<=0){
            perror("error receiving test messages");
            close(sfd);
            close(udpfd);
            return -1;
        }
        if(strcmp((char*)msgbuf,"testu")==0){
            memset(msgbuf, 0, BUFFSIZE);
            memcpy(msgbuf,&count, sizeof(count));
            if(sendto(sfd,msgbuf, sizeof(count), 0, NULL, 0)<0){
                perror("error sending recv speed test feedback\n");
                close(sfd);
                close(udpfd);
                return -1;
            }
        }
        else if(strstr((char*)msgbuf,"proceed:")!=NULL){//jump to next move;
            memcpy(&dlto,msgbuf+8, sizeof(int));
            //printf("Timeout:%d\n",dlto);
            break;
        }
        else if(strcmp((char*)msgbuf,"end")==0){
            close(sfd);
            close(udpfd);
            perror("end message received too early");
            return -1;
        }
    }
    printf("Speedtest upload completed.\n");
    return 0;
}
int scanhandler(struct threaddata* td){
    int sfd=td->sfd;
    sockaddr_in* msgaddr=td->msgaddr;
    socklen_t msgaddrlen= sizeof(*msgaddr);
    int port=td->port;
    int portend=td->portend;
    struct timeval tv=td->tv;
    printf("Client Thead Scanning from %d to %d.\n", port, portend);
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
            continue;
        }
        ++port;
        close(fd);
    }
    char* endmsg="end";
    sendto(sfd,endmsg, strlen(endmsg),0, NULL, 0);
    close(sfd);
    printf("Scan session ends.\n");
    delete td;
    return 0;
}
int taskHandler(struct pshdata *pshd){
    int sfd;
    unsigned char msgbuf[BUFFSIZE];
    int recvlen;
    int port, portend;
    int fd=pshd->fd;
    struct sockaddr_in addr=pshd->addr;
    socklen_t addrlen=pshd->addrlen;
    while(work){
        if((sfd=accept(fd,(struct sockaddr*)&addr,&addrlen))<0){
            perror("Error Accepting PortScan MSG socket.\n");
            return -1;
        }
        recvlen=recvfrom(sfd, msgbuf, BUFFSIZE, 0, (struct sockaddr*)&addr, &addrlen);
        if(recvlen>0){
            //process request, process port number;
            char *pivot=strstr((char *)msgbuf,"request:");
            if(pivot==NULL||pivot!=(char*)msgbuf){
                //bad message
                memset(msgbuf,0,BUFFSIZE);
                close(sfd);
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
            printf("Received Client Scan Request\n");
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
            td->tv=pshd->tv;
            std::thread scanthread(scanhandler,td);
            printf("Client Scan Thread Created.\n");
            scanthread.detach();
            //Do sth else;
            //
        }
    }
    delete pshd;
    return 0;
}
int sptHandler(struct sptdata* sptd){
    int sfd;
    unsigned char msgbuf[BUFFSIZE];
    int fd=sptd->fd;
    struct sockaddr_in* addr=sptd->addr;
    socklen_t addrlen=sptd->addrlen;
    int recvlen;
    struct timeval tv0;
    tv0.tv_usec=0;
    tv0.tv_sec=0;

    //initial  udpaddr
    int udpfd;
    int dlto;
    struct sockaddr_in udpaddr;
    socklen_t udpaddrlen= sizeof(udpaddr);


    while(work) {
        if ((sfd = accept(fd, (struct sockaddr *) addr, &addrlen)) < 0) {
            perror("Error Accepting TestRunning MSG socket.\n");
            return -1;
        }

        struct timeval tv;
        tv.tv_sec=0;
        tv.tv_usec=0;
        memset(msgbuf, 0, BUFFSIZE);
        recvlen=recvfrom(sfd, msgbuf, BUFFSIZE, 0, NULL, NULL);
        if(recvlen>0){
            if(!strcmp((char*)msgbuf, "start")){//incoming message is start
                printf("Received Speed Testing Request.\n");
                int attempt=0;
                bool portfind=false;
                int portno;
                srand(time(NULL));
                while(attempt<PORTALLO&&!portfind){
                    if((udpfd=socket(AF_INET, SOCK_DGRAM,0))<0) {
                        perror("Unable to start UDP Speed Testing socket");
                        break;
                    }
                    portno=DYMPORTL+rand()%(DYMPORTR-DYMPORTL+1);
                    memset((char*)&udpaddr, 0, sizeof(udpaddrlen));
                    udpaddr.sin_family=AF_INET;
                    udpaddr.sin_addr.s_addr= htonl(INADDR_ANY);
                    udpaddr.sin_port=htons(portno);
                    if((bind(udpfd, (struct sockaddr*)&udpaddr, sizeof(udpaddr)))<0){
                        perror("Unable to bind UDP Speed Testing socket");
                        attempt++;
                        close(udpfd);
                        continue;
                    }
                    else{
                        portfind=true;
                        if(setsockopt(udpfd,SOL_SOCKET, SO_RCVTIMEO, &tv0, sizeof(tv0))<0){
                            perror("error setting udp speedtest socket option");
                        }
                    }
                }
                memset(msgbuf,0,BUFFSIZE);
                if(portfind==true){
                    //send ACK and portno
                    sprintf((char*)msgbuf,"ACK:%d",portno);
                }
                else{
                    //send NACK
                    sprintf((char*)msgbuf,"NACK");
                }
                if(sendto(sfd, msgbuf, strlen((char*)msgbuf), 0, NULL, 0)<0)
                {
                    perror("ACK/NACK for Speed Testing client not sent.\n");
                    close(udpfd);
                    close(sfd);
                    continue;
                }
                //upload speedtest
                printf("Starting Upload SpeedTest.\n");
                if(speedtestrecv_s(udpfd, &udpaddr, sfd, (char*) msgbuf, tv0, dlto)<0){
                    close(udpfd);
                    close(sfd);
                    continue;
                }
                //download speedtest
                if(speedtestsend_s(udpfd, &udpaddr, sfd, (char*) msgbuf,dlto)<0){
                    close(udpfd);
                    close(sfd);
                    continue;
                }

                close(udpfd);
                close(sfd);
                continue;
            } else{
                printf("Bad speedtest start message.\n");
                close(sfd);
                continue;
            }
        }
        else{
            perror("Control link broken");
        }

    }
    delete sptd;
}
int main(int argc, char *argv[]) {
    //initialize port scan and delay test parameters
    int fd;
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
    int optps=1;//

    signal(SIGPIPE, SIG_IGN);

    if((fd=socket(AF_INET, SOCK_STREAM,0))<0) {
        perror("Unable to start TCP msg portscan socket");
        return -1;
    }
    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr))<0){
        perror("Unable to bind msg socket");
        return -1;
    }
    if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR ,&optps, sizeof(optps))){
        perror("Error setting options for TCP socket");
        return -1;
    }

    if(listen(fd,3)<0){
        perror("Fail to listen to MSGPORT");
    }

    //set values to pass to the scanhandler thread
    struct pshdata *pshd= new struct pshdata;
    pshd->addrlen=addrlen;
    pshd->addr= addr;
    pshd->tv=tv;
    pshd->fd=fd;

    printf("Server started, waiting for clients:\n");
    std::thread taskhander(taskHandler,pshd);
    printf("TaskHandler Thread Created\n");
    taskhander.detach();
    int fd2;
    int optspt=1;
    //initialize speed test parameters

    struct sockaddr_in addr2;
    socklen_t addr2len= sizeof(addr2);
    //set socket
    memset((char*)&addr2, 0, sizeof(addr2));
    addr2.sin_family=AF_INET;
    addr2.sin_addr.s_addr= htonl(INADDR_ANY);
    addr2.sin_port=htons(SPTPORT);

    if((fd2=socket(AF_INET, SOCK_STREAM,0))<0) {
        perror("Unable to start TCP msg speed test socket");
        return -1;
    }
    if(bind(fd2, (struct sockaddr*)&addr2, sizeof(addr2))<0){
        perror("Unable to bind speed socket");
        return -1;
    }
    if(setsockopt(fd2,SOL_SOCKET,SO_REUSEADDR,&optspt, sizeof(optspt))){
        perror("Error setting options for TCP socket");
        return -1;
    }
    if(listen(fd2,3)<0){
        perror("Fail to listen to SPTPORT");
    }
    struct sptdata *sptd= new struct sptdata;
    sptd->fd=fd2;
    sptd->addr=&addr2;
    sptd->addrlen=addr2len;
    std::thread spdtesthander(sptHandler,sptd);
    printf("SpeedTest Handler Thread Created\n");
    spdtesthander.detach();

    while(work){}
    printf("Server Quit. Bye!!\n");
    return 0;
}
