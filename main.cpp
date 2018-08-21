#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstring>
#include <unistd.h>
#include <sstream>
#include <thread>
#include <numeric>
#include <chrono>
#include <ctime>
#include <errno.h>
#include <stdio.h>
#include <string>
#include "gnuplot-iostream.h"

#define BUFFSIZE 64
#define PKTBUFSIZE 1000
#define WAVEQUOTE 600
#define TIMEOUT_MS 3500
#define SERVERPORT 50025
#define SPEEDPORT 50027
#define DROPTHRESHOLD 0.005
#define DROPTHRURGENT 0.01
#define INTAJDTHRESHOLD 5
#define BASEINTERVAL 80
#define DEFINTERVAL 1000
#define SPTTIMEOUT 80
#define PORTALLO 10
using namespace std;
int portscanstatus=-1;//only -1, 0, 1 is in use.
/*
 * -1: portscan not yet start
 * 0:  portscan ended
 * 1:  portscan begin
 * */
struct portscandata{
    int port;
    int portend;
    char* servname;
    FILE *f;
};
struct speedtestdata{
    char* servname;
    int dltimeout;
};
struct upltestdata{
    sockaddr_in addr;
    int port;
    int sfd;
    int fd;
};
struct dnltestdata{
    sockaddr_in addr;
    int port;
    int sfd;
    int fd;
};
class Plotter{
private:
    vector<double> vec1;
    vector<double> vec2;
    int count=500;
    string title;
public:
    void setTitle(string s){
        this->title=s;
    }
    pair<vector<double>, vector<double>> getPlotData(){
        return make_pair(vec1,vec2);
    };
    void appendData(double data1, double data2){
        int l=vec1.size();
        if(l==count){
            vec1.erase(vec1.begin());
            vec2.erase(vec2.begin());
        }
        vec1.push_back(data1);
        vec2.push_back(data2);
    }
};
int portscanclient(struct portscandata* psd){
    int fd,streamfd;
    FILE *f=psd->f;
    char* servname=psd->servname;
    int port=psd->port;
    int portend=psd->portend;
    struct hostent *hp;
    struct sockaddr_in servaddr, servudpaddr;
    socklen_t servsocklen=sizeof(servaddr);
    socklen_t servudpsocklen=sizeof(servudpaddr);
    int opt=1;
    unsigned char buf[BUFFSIZE];
    /*Set two timers for each session*/
    struct timeval tv1;
    tv1.tv_sec=TIMEOUT_MS/3/1000;
    tv1.tv_usec=1000*(TIMEOUT_MS/3-(TIMEOUT_MS/3/1000)*1000);
    struct timeval tv2;
    tv2.tv_sec=TIMEOUT_MS*2/3/1000;
    tv2.tv_usec=1000*(TIMEOUT_MS*2/3-(TIMEOUT_MS*2/3/1000)*1000);
    struct timeval tvmsg;
    tvmsg.tv_sec=7;
    tvmsg.tv_usec=0;
    string end="end\n";

    hp=gethostbyname(servname);
    if(!hp){
        perror("Host not found");
        return 0;
    }
    //begin send message
    memset(buf,0,BUFFSIZE);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(SERVERPORT);
    memcpy((void *)&servaddr.sin_addr,hp->h_addr_list[0], hp->h_length);
    char msg[BUFFSIZE];
    if((streamfd=socket(AF_INET,SOCK_STREAM,0))<0){
        perror("Unable to start TCP msg socket");
        return -1;
    }
    if(connect(streamfd,(struct sockaddr*)&servaddr, sizeof(servaddr))<0){
        perror("Server is not open");
        return 0;
    }
    sprintf(msg,"request:%d-%d\n",port,portend);
    fputs(msg,f);
    if((sendto(streamfd, msg, strlen(msg), 0, (struct sockaddr*)&servaddr, servsocklen))<0){
        perror("Message sending error");
        close(streamfd);
        return 0;
    }
    printf("Request sent:%d-%d\n",port,portend);
    memset(msg,0,BUFFSIZE);
    if((recvfrom(streamfd,msg,BUFFSIZE,0,(struct sockaddr*)&servaddr,&servsocklen))<0){
        perror("NO ACK RECEIVED.");
        return 0;
    }
    if(strcmp(msg,"OK")!=0){
        perror("Server did not reply ACK");
        return 0;
    }
    //begin scanning
    portscanstatus=1;
    printf("Scanning begin:\n");
    char tofile[BUFFSIZE];
    char* test=new char;
    int total=portend-port+1;
    int count=0;
    double totaldelay=0;
    double prevdelay=-1;
    double jitter=0;
    while(port<=portend){
        /*if(port==SERVERPORT) {//avoid the server message port;
            ++port;
            usleep(1500000);
            continue;
        }*/
        //hear from TCP:
        memset(msg,0,BUFFSIZE);
        if((recvfrom(streamfd,msg,BUFFSIZE,0,(struct sockaddr*)&servaddr,&servsocklen))<0){
            perror("TCP message link broken during scan.\n");
            return -1;
        }
        if(strcmp(msg,"end")==0){
            fclose(f);
            printf("Server says end.\n");
            printf("Last scanned port %d.\n", port);
            fputs(end.c_str(),f);
            fclose(f);
            close(streamfd);
            return 0;
        }else{
            port=atoi(msg);
            if(port>portend||port<=0)
                continue;
            //printf("Scanning Port %d\n", port);
        }
        memset(buf,0,BUFFSIZE);
        memset(&servudpaddr, 0, sizeof(servudpaddr));
        servudpaddr.sin_family=AF_INET;
        servudpaddr.sin_port=htons(port);
        memcpy((void *)&servudpaddr.sin_addr,hp->h_addr_list[0], hp->h_length);

        int testlen=0;
        testlen=sprintf(test,"testmsg:%d",port);

        if((fd=socket(AF_INET,SOCK_DGRAM,0))<0)
            perror("Unable to start UDP socket");
        if((setsockopt(fd,SOL_SOCKET, SO_RCVTIMEO,&tv1, sizeof(tv1)))<0)
            perror("error setting recv timout");
        if((sendto(fd, test, testlen, 0, (struct sockaddr*)&servudpaddr, servudpsocklen))<0){
            printf("Port No.%d is not reachable.\n",port);
            //++port;
            close(fd);
            continue;
        }
        auto start = std::chrono::system_clock::now();
        if((recvfrom(fd,buf, BUFFSIZE, 0, (struct sockaddr*)&servudpaddr, &servudpsocklen))<=0){
            //++port;
            close(fd);
            continue;

        }
        auto end = std::chrono::system_clock::now();
        std::chrono::duration<double> diff = end-start;
        /*printf("Received message:%s\n",buf);*/
        //printf("Message in port %d: %s \n",port,buf);
        //printf("Port %d is alive. Measured Delay: %fms.\n", port, 1000*diff.count());
        memset(tofile,0,BUFFSIZE);
        sprintf(tofile,"%d\n", port);
        fputs(tofile,f);
        count++;
        totaldelay+=1000*diff.count();
        if(prevdelay!=-1)
            jitter+=1000*diff.count()-prevdelay>0 ? 1000*diff.count()-prevdelay : prevdelay-1000*diff.count();
        prevdelay=1000*diff.count();
        close(fd);
        if(port==portend)
            break;
    }

    printf("%d/%d ports reported open.\n", count, total);
    printf("Average Delay: %fms.\n", totaldelay/count);
    printf("Average Jitter: %fms.\n", jitter/count);
    printf("Scanning Request Ends.\n");
    portscanstatus=0;
    fputs(end.c_str(),f);
    fclose(f);
    close(streamfd);
    delete test;
    return 0;
}
void randpayloadset(char* payload, size_t len){
    srand(time(NULL));
    for(int i=0; i<len;++i){
        *(payload+i)=(char)(rand()%256);
    }
}
int upltestthread(struct upltestdata *uptd){
    int fd=uptd->fd;
    sockaddr_in addr=uptd->addr;
    socklen_t socklen= sizeof(addr);
    int sfd=uptd->sfd;
    bool go_on=true;

    return 0;

}
int speedtestrecv_c(int udpfd, sockaddr_in* udpaddr, int sfd, char* msgbuf, struct timeval tv0){
    int readlen=0, count=0;
    char buf[PKTBUFSIZE];
    fd_set udpreadset, tcpreadset;
    int ures=0,tres=0;
    socklen_t udpaddrlen= sizeof(*udpaddr);
    auto tickcount=std::chrono::system_clock::now();
    auto ticknow=std::chrono::system_clock::now();
    auto pipetimeout=std::chrono::system_clock::now();
    std::chrono::duration<double> diff;
    double timeline=0;
    double roundtime;
    double dt;
    Gnuplot g_down;
    vector<double> vec1;
    vector<double> vec2;
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
                    if(readlen==PKTBUFSIZE){
                        count++;
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

        } while ((tres==-1 &&errno==EINTR)||tres == 0);
        ticknow=std::chrono::system_clock::now();
        //process http packet if
        memset(msgbuf,0 ,BUFFSIZE);
        //FIXME: We didn't check the FD_ISSET as only 1 socket is detected.
        int l=recvfrom(sfd, msgbuf, BUFFSIZE, 0, NULL, NULL);
        if(l<=0){
            perror("error receiving test messages");
            //FIXME: How to handle;
        }
        diff=ticknow-tickcount;
        dt=diff.count()-timeline;
        //printf("%f %d\n",timeline, count*8);

        char* pivot;
        pivot=strstr(msgbuf,"testu:");
        if(pivot==msgbuf){
            //extract statistics
            memcpy(&timeline,msgbuf+strlen("testu:"), sizeof(timeline));
            memcpy(&roundtime,msgbuf+strlen("testu:")+sizeof(timeline), sizeof(roundtime));
            memset(msgbuf, 0, BUFFSIZE);
            memcpy(msgbuf,&count, sizeof(count));
            if(sendto(sfd,msgbuf, sizeof(count), 0, NULL, 0)<0){
                perror("error sending recv speed test feedback\n");
                close(sfd);
                close(udpfd);
                return -1;
            }
        }
        else{//process result;
            double rate, lossrate;
            memcpy(&rate, msgbuf, sizeof(rate));
            memcpy(&lossrate, msgbuf+sizeof(rate), sizeof(lossrate));
            char output[BUFFSIZE];
            sprintf((char*)output,"Download Rate: %f kbps.\n", rate);
            printf(output);
            sprintf((char*)output,"Download packet loss rate %f.\n", lossrate);
            printf(output);
            break;
        }
        vec1.push_back(timeline);
        vec2.push_back(count*8/roundtime);
        g_down <<"set title'Downlink Bandwidth Test'\n";
        g_down <<"set xlabel 'Time(s)'\n";
        g_down <<"set ylabel 'Bandwidth kbps'\n";
        g_down <<"set key right bottom\n";
        g_down <<"plot '-' using 1:2 w lines tit 'Downlink Bandwidth'\n";
        g_down.send1d(make_pair(vec1,vec2));
        g_down.flush();
    }
    printf("Speedtest download completed.\n");
    return 0;
}
int speedtestsend_c(int udpfd, sockaddr_in* addr, int streamfd, char* msg,int timeout){
    char ubuf[PKTBUFSIZE];
    randpayloadset((char*)ubuf, PKTBUFSIZE);
    auto start = std::chrono::system_clock::now();
    auto end=std::chrono::system_clock::now();
    std::chrono::duration<double> diff = end-start;
    std::chrono::duration<double> diff2;
    std::chrono::duration<double> timeline;
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
    int countzeros=0;
    double rate=0;
    string proceedstr="proceed";
    string testu="testu";
    string endstr="end";
    double adaptivesleep=DEFINTERVAL;
    Plotter pu;
    pu.setTitle("Upload");
    vector<double> vec1;
    vector<double> vec2;
    printf("Entering upload testing cycle.\n");
    Gnuplot g_up;
    if(timeout<SPTTIMEOUT)
        timeout=SPTTIMEOUT;
    while(diff.count()<timeout){//default timeout 30s
        quote=temp;
        auto tick1=std::chrono::system_clock::now();
        printf("%f\n",adaptivesleep);
        while(quote>0){
            sentl=sendto(udpfd,ubuf,PKTBUFSIZE,0,(sockaddr*)addr, socklen);
            if(sentl<=0) {
                perror("Uplink speed testing udp broken");
            }
            else{
                count++;
                quote--;
            }
            std::this_thread::sleep_for(std::chrono::microseconds((int)adaptivesleep));
        }
        auto tick2=std::chrono::system_clock::now();
        //diff2 = tick2-tick1;
        //sumtime+=diff2.count();
        //send current sent quote to tcp socket
        memset(msg,0,BUFFSIZE);
        if((sendto(streamfd, testu.c_str(), strlen(testu.c_str()), 0, NULL, 0))<0){
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
        diff2 = tick2-tick1;
        sumtime+=diff2.count();
        memcpy(&feedback,msg, sizeof(feedback));
        //compute loss;
        waveloss=temp-feedback;
        //pu.appendData(sumtime,feedback*8/diff2.count());
        timeline=tick2-start;
        vec1.push_back(timeline.count());
        vec2.push_back(feedback*8/diff2.count());
        //plot data
        g_up <<"set title'Uplink Bandwidth Test'\n";
        g_up <<"set xlabel 'Time(s)'\n";
        g_up <<"set ylabel 'Bandwidth kbps'\n";
        g_up <<"set key right bottom\n";
        g_up <<"plot '-' using 1:2 w lines tit 'Uplink Bandwidth'\n";
        g_up.send1d(make_pair(vec1,vec2));
        g_up.flush();
        loss+=waveloss;
        if(feedback>temp){
            printf("Some packet out of sequence\n");
            adaptivesleep=adaptivesleep*1.1;
            countzeros=0;
        }
        if(waveloss>=0){
            lossbalance=lossbalance+(waveloss-lossbalance)/4;
        }
        //adjust timer
        if(lossbalance>DROPTHRURGENT*temp && waveloss>DROPTHRURGENT*temp&& intadjcount<INTAJDTHRESHOLD){
            adaptivesleep*=4;
            intadjcount+=2;
        }
        else if(lossbalance>DROPTHRESHOLD*temp && waveloss>DROPTHRESHOLD*temp&& intadjcount<INTAJDTHRESHOLD){
            adaptivesleep*=2;
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


    proceedstr+=":";
    memset(msg,0,BUFFSIZE);
    memcpy(msg,proceedstr.c_str(), strlen(proceedstr.c_str()));
    memcpy(msg+strlen(proceedstr.c_str()), &timeout, sizeof(timeout));
    if((sendto(streamfd, msg, strlen(proceedstr.c_str())+sizeof(timeout), 0, NULL, 0))<0){
        perror("Message sending error: Upload test end/proceed to next message not sent.\n");
        close(streamfd);
        close(udpfd);
        return -1;
    }

    rate=count*8/sumtime;
    char output[BUFFSIZE];
    sprintf((char*)output,"Upload Rate: %f kbps.\n", rate);
    printf(output);
    sprintf((char*)output,"Upload packet loss rate %f.\n", (double)loss/count);
    printf(output);
}
int speedtestclient(struct speedtestdata* psd){
    int streamfd;
    int dltimeout=psd->dltimeout;
    char* servname=psd->servname;
    struct hostent *hp;
    struct sockaddr_in servaddr, servudpaddr;
    socklen_t servsocklen=sizeof(servaddr);
    socklen_t servudpsocklen=sizeof(servudpaddr);
    int opt=1;
    unsigned char buf[BUFFSIZE];
    /*Set two timers for each session*/
    struct timeval tv1;
    tv1.tv_sec=0;
    tv1.tv_usec=0;
    hp=gethostbyname(servname);
    if(!hp){
        perror("Host not found");
        return -1;
    }
    //begin send message
    memset(buf,0,BUFFSIZE);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(SPEEDPORT);
    memcpy((void *)&servaddr.sin_addr,hp->h_addr_list[0], hp->h_length);
    char msg[BUFFSIZE];
    if((streamfd=socket(AF_INET,SOCK_STREAM,0))<0){
        perror("Unable to start TCP msg socket");
        delete psd;
        return -1;
    }
    if(connect(streamfd,(struct sockaddr*)&servaddr, servsocklen)<0){
        perror("Server is not open");
        close(streamfd);
        delete psd;
        return 0;
    }
    string startmsg="start";
    if((sendto(streamfd, startmsg.c_str(), strlen(startmsg.c_str()), 0, NULL, 0))<0){
        perror("Message sending error: upload start message");
        close(streamfd);
        delete psd;
        return 0;
    }
    printf("SpeedTest Start Sent.\n");
    memset(msg,0,BUFFSIZE);
    if((recvfrom(streamfd,msg,BUFFSIZE,0, NULL, NULL))<0){
        perror("NO ACK RECEIVED.");
        close(streamfd);
        delete psd;
        return 0;
    }
    if(strcmp(msg,"NACK")==0){
        perror("Server Replied NACK. STOP.\n");
        close(streamfd);
        delete psd;
        return 0;
    }
    //
    int udpport;
    char *pivot=strstr((char *)msg,":");
    if(pivot==NULL){
        perror("UDP PORT message wrong format.\n");
        close(streamfd);
        delete psd;
        return 0;
    }
    else{
        udpport=atoi(pivot+1);
    }
    printf("Received UDP SPEED TESTING PORT %d.\n", udpport);

    //initial an upload thread data;
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_port=htons(udpport);
    memcpy((void *)&addr.sin_addr,hp->h_addr_list[0], hp->h_length);
    socklen_t socklen=sizeof(addr);
    //initiate udp sockets speed testing
    int udpfd;
    if((udpfd=socket(AF_INET,SOCK_DGRAM,0))<0){
        perror("Unable to start UDP socket");
        close(streamfd);
        close(udpfd);
        delete psd;
        return 0;
    }
    if((setsockopt(udpfd,SOL_SOCKET, SO_RCVTIMEO,&tv1, sizeof(tv1)))<0){
        perror("error setting recv timout");
        delete psd;
        close(streamfd);
        close(udpfd);
        return 0;
    }
    //Begin sending process
    if(speedtestsend_c(udpfd, &addr, streamfd,(char*)msg, dltimeout)<0){
        delete psd;
        close(streamfd);
        close(udpfd);
        return 0;
    }
    //try download speed;
    if(speedtestrecv_c(udpfd, &addr, streamfd, (char*)msg, tv1)<0){
        delete psd;
        close(streamfd);
        close(udpfd);
        return 0;
    }

    close(streamfd);
    close(udpfd);
    delete psd;
    return 0;

}
int main(int argc, char *argv[]) {
    if(argc<5){
        perror("Invalid number of arguments");
        return 0;
    }
    char* servname=argv[1];
    int port;
    int portend;
    if(argv[2]<=argv[3]){
        port=atoi(argv[2]);
        portend=atoi(argv[3]);
    }
    else{
        port=atoi(argv[2]);
        portend=atoi(argv[3]);
    }
    char* filename=argv[4];
    FILE *f;
    time_t now=time(0);
    char* timechar=ctime(&now);
    printf("%s\n",timechar);
    if(argc==6&&strcmp(argv[5],"-C")==0) {
        f = fopen(filename, "w");
        fputs(timechar,f);
    }
    else{
        f=fopen(filename,"a");
        fputs(timechar,f);
    }
    //activate speedtest client
    int timeout=180;
    struct speedtestdata* sptd= new struct speedtestdata;
    sptd->servname=servname;
    sptd->dltimeout=timeout;
    std::thread sptclient(speedtestclient, sptd);
    sptclient.join();
    //activate portscanclient
    struct portscandata* psd= new struct portscandata;
    psd->port=port;
    psd->portend=portend;
    psd->servname=servname;
    psd->f=f;
    std::thread psclient(portscanclient,psd);
    psclient.join();
}
