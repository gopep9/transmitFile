#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <vector>
#include <sstream>
#include <string.h>
#include <stdlib.h>

//using namespace std;
static pthread_mutex_t g_mutex;
static pthread_cond_t g_cond;

static std::string downloadPath;
void *acceptFileThread(void *arg)
{
    pthread_mutex_lock(&g_mutex);
    int connectSock=*(int*)arg;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);
    char buf[1025]={};
    int readwordnum=0;
    chdir(downloadPath.c_str());
    while(true)//可以弄成每个文件一个循环
    {
        int headSize=1024;
        std::string fileMessage;
        while(headSize>0)
        {
            if(headSize>=1024)
            {
                memset(buf,0,1024);
                readwordnum=read(connectSock,buf,1024);
                if(readwordnum==0)
                {
                    //读取结束
                    return NULL;
                }
                fileMessage+=buf;
                headSize-=readwordnum;
                memset(buf,0,1024);
            }else
            {
                memset(buf,0,1024);
                readwordnum=read(connectSock,buf,headSize);
                //要对null情况进行处理
                if(readwordnum==0)
                {
                    printf("error for transmit\n");
                    return NULL;
                }
                fileMessage+=buf;
                headSize-=readwordnum;
                memset(buf,0,1024);
            }
        }
        //这里要保证是buf以零结尾
        std::cout<<"receive head:"<<fileMessage;
        size_t nameStartPos = fileMessage.find("file:")+5;
        size_t nameEndPos = fileMessage.find_first_of(";");
        std::string filename=fileMessage.substr(nameStartPos,nameEndPos-nameStartPos);
        size_t sizeStartPos=fileMessage.find("size:")+5;
        size_t sizeEndPos=fileMessage.find_last_of(";");
        std::string filesize=fileMessage.substr(sizeStartPos,sizeEndPos-sizeStartPos);
        std::istringstream filesizestream(filesize);
        std::cout<<"fileName:"<<filename<<std::endl;
        std::cout<<"size:"<<filesize<<std::endl;
        //在这里创建文件夹和打开文件
        int posOfFengGeFu=0;
        char oldCurrentPath[PATH_MAX]={};
        if(getcwd(oldCurrentPath, PATH_MAX)==NULL)
        {
            printf("getcwd error");
        }
        //在这里验证文件路径是否超出限制PATH_MAX
        if(strlen(oldCurrentPath)+filename.size()>=PATH_MAX)
        {
            printf("error,file path out of max_path\n");
            std::cout<<filename<<std::endl;
            return NULL;
        }
        
        while ((posOfFengGeFu=filename.find("/"))!=-1) {
            std::string pathName=filename.substr(0,0+posOfFengGeFu);
            filename=filename.substr(posOfFengGeFu+1);
            //            忽略当前目录
            if(pathName==".")
                continue;
            if(access(pathName.c_str(), F_OK)!=0)
            {//在没有的情况下才创建目录
                mkdir(pathName.c_str(), 0777);
            }
            chdir(pathName.c_str());
        }
        int fd=open(filename.c_str(),O_WRONLY|O_CREAT,0666);
        if(fd<0)
        {
            printf("create file failed\n");
            return NULL;
        }
        int filesizeint=0;
        filesizestream>>filesizeint;
        while (filesizeint>0) {
            //可能不能保证每次读取1024
            if(filesizeint>=1024)
            {
                readwordnum=read(connectSock,buf,1024);
                if(readwordnum==0)
                {
                    printf("error for transmit\n");
                    return NULL;
                }
                write(fd,buf,readwordnum);
                filesizeint-=readwordnum;
            }else
            {
                readwordnum=read(connectSock,buf,filesizeint);
                if(readwordnum==0)
                {
                    printf("error for transmit\n");
                    return NULL;
                }
                write(fd, buf, readwordnum);
                filesizeint-=readwordnum;
            }
        }
        printf("accept complete\n");
        chdir(oldCurrentPath);
        close(fd);
    }
    return NULL;
}

#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
int addDocumentToList(std::string document,std::vector<std::string> *fileList)
{
    DIR *dfd;
    char name[MAX_PATH];
    struct dirent *dp;
    if((dfd=opendir(document.c_str()))==NULL)
    {
        perror("can't open dir");
        return 1;
    }
    while((dp=readdir(dfd))!=NULL)
    {
        //不遍历.和..目录
        if((strcmp(dp->d_name, "." )==0)||(strcmp(dp->d_name, ".." )==0))
            continue;
        if(document.size()+strlen(dp->d_name)+2>sizeof(name))
        {
            printf("dir_order: name %s %s too long\n",document.c_str(),dp->d_name);
        }
        else
        {
            std::string fullFilePath=document+"/"+dp->d_name;
            struct stat fileStat={};
            if(lstat(fullFilePath.c_str(), &fileStat)<0)
            {
                printf("错误码：%d，文件可能不存在",errno);
                continue;
            }
            if(S_ISREG(fileStat.st_mode))
            {
                fileList->push_back(fullFilePath);
            }
            else if(S_ISDIR(fileStat.st_mode))
            {
                addDocumentToList(fullFilePath,fileList);
            }
        }
    }
    return 0;
}

int transmitFile(int sock,std::string transFile,std::string fileName)
{
    struct stat fileStat={};
    if(lstat(transFile.c_str(), &fileStat)<0)
    {
        printf("错误码：%d，文件可能不存在",errno);
    }
    
    int fd=open(transFile.c_str(),O_RDONLY);
    if(fd<0)
    {
        printf("open file failed");
        return -1;
    }
    char buf[1024]={};
    
    std::string headStr;
    std::stringstream headStrStream;
    headStrStream<<std::string("file:")<<fileName<<";"<<"size:"<<fileStat.st_size<<";";
    headStr=headStrStream.str();
    //第一次的时候传一个1024大小的头部
    strcpy(buf, headStr.c_str());
    write(sock,buf,1024);
    int readwordnum=0;
    while((readwordnum=read(fd,buf,1024))>0)
    {
        write(sock, buf, readwordnum);
    }
    close(fd);
    return 0;
}


int main(int argc,char *argv[])
{
    if(argc!=3)
    {
        std::cout<<"first arg is lister port\n"
        <<"second arg is path download The file\n";
        exit(1);
    }
    std::string listenPort=argv[1];
    downloadPath=argv[2];
    
    int listenSock=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(listenSock < 0)
    {
        perror("socket");
        exit(1);
    }
    struct sockaddr_in listenSockaddr={};
    listenSockaddr.sin_family=AF_INET;
    listenSockaddr.sin_port=htons(atoi(listenPort.c_str()));
    listenSockaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    socklen_t len=sizeof(listenSockaddr);
    if(bind(listenSock, (struct sockaddr*)&listenSockaddr, len)<0)
    {
        perror("bind");
        exit(2);
    }
    
    if(listen(listenSock,5)<0)
    {
        perror("listen");
        exit(3);
    }
    
    struct sockaddr_in remoteSockaddr={};
    pthread_mutex_init(&g_mutex, NULL);
    pthread_cond_init(&g_cond, NULL);
    //设置listenSock不阻塞
    //    int flags=fcntl(listenSock, F_GETFL);
    //    flags|=O_NONBLOCK;
    //    fcntl(listenSock, F_SETFL,flags);
    while(true){
        
        int connectSock=accept(listenSock, (struct sockaddr*)&remoteSockaddr, &len);
        if(connectSock<0)
        {
            perror("accept");
            return -1;
        }
        //在这里服务端传文件给客户端
        std::cout<<"send or receive(enter s or r)\n";
        std::string transmitType;
        std::getline(std::cin, transmitType);
        if(transmitType=="s")
        {
            std::cout<<"enter the file or document which want to transmit\n";
            std::string localFileName;
            std::getline(std::cin,localFileName);
            std::cout<<"enter the filename in remote\n";
            std::string remoteFileName;
            std::getline(std::cin, remoteFileName);
            struct stat fileStat={};
            if(lstat(localFileName.c_str(), &fileStat)<0)
            {
                perror("file not exist");
                return 1;
            }
            if(S_ISREG(fileStat.st_mode))
            {
                transmitFile(connectSock, localFileName, remoteFileName);
            }
            else if(S_ISDIR(fileStat.st_mode))
            {
                std::vector<std::string>fileList;
                if(localFileName[localFileName.size()-1]=='/')
                {
                    localFileName.erase(localFileName.end()-1);
                }
                char currentWorkPath[PATH_MAX]={};
                if(getcwd(currentWorkPath, PATH_MAX)==NULL)
                {
                    printf("getcwd error");
                }
                chdir(localFileName.c_str());
                addDocumentToList(".", &fileList);
                chdir(currentWorkPath);
                
                for(std::vector<std::string>::iterator it=fileList.begin();it!=fileList.end();it++)
                {
                    std::string tmp=*it;
                    //去除开头的./
                    if(it->substr(0,2)=="./")
                        it->erase(0, 2);
                    transmitFile(connectSock, localFileName+"/"+*it, remoteFileName+"/"+*it);
                }
            }
        }
        else if(transmitType=="r")
        {
            std::cout<<"receive file or document from ip address: "<<inet_ntoa(remoteSockaddr.sin_addr)<<"\nand port: "<<ntohs(remoteSockaddr.sin_port)<<"\n";
            pthread_t pthread_tid=0;
            pthread_mutex_lock(&g_mutex);
            pthread_create(&pthread_tid, nullptr, acceptFileThread, &connectSock);
            pthread_cond_wait(&g_cond, &g_mutex);
            pthread_mutex_unlock(&g_mutex);
            //等待线程结束
            pthread_join(pthread_tid,NULL);
        }
        else
        {
            puts("error should input s or r");
        }
        close(connectSock);
        std::cout<<"done\n";
        sleep(1);
        
    }
    return 0;
}
