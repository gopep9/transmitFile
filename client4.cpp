#include <iostream>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <utility>
#include <sstream>
#include <vector>
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
    char buf[2048+1]={};
    int readwordnum=0;
    chdir(downloadPath.c_str());
    while(true)//可以弄成每个文件一个循环
    {
        int headSize=2048;
        std::string fileMessage;
        while(headSize>0)
        {
            if(headSize>=2048)
            {
                readwordnum=read(connectSock,buf,2048);
                if(readwordnum==0)
                {
                    //读取结束
                    return NULL;
                }
                fileMessage+=buf;
                headSize-=readwordnum;
                memset(buf,0,2048);
            }else
            {
                readwordnum=read(connectSock,buf,headSize);
                //要对null情况进行处理
                if(readwordnum==0)
                {
                    printf("error for transmit");
                    return NULL;
                }
                fileMessage+=buf;
                headSize-=readwordnum;
                memset(buf,0,2048);
            }
        }
        //这里要保证是buf以零结尾
        std::cout<<"receive head:"<<fileMessage;
        if(fileMessage[0]=='d')
        {
            //目录的情况
            size_t nameStartPos = fileMessage.find("document:")+9;
            size_t nameEndPos = fileMessage.find_first_of(";");
            std::string filename=fileMessage.substr(nameStartPos,nameEndPos-nameStartPos);
            
            //创建文件目录
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
            //返回原来的目录
            chdir(oldCurrentPath);
        }
        else if(fileMessage[0]=='f'){
            //文件的情况
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
                //可能不能保证每次读取2048
                if(filesizeint>=2048)
                {
                    readwordnum=read(connectSock,buf,2048);
                    if(readwordnum==0)
                    {
                        printf("error for transmit");
                        return NULL;
                    }
                    write(fd,buf,readwordnum);
                    filesizeint-=readwordnum;
                }else
                {
                    readwordnum=read(connectSock,buf,filesizeint);
                    if(readwordnum==0)
                    {
                        printf("error for transmit");
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
    }
    return NULL;
}


int addDocumentToList(std::string document,std::vector<std::string> *fileList)
{
    //在这里添加当前目录进入文件列表
    fileList->push_back(document);
    DIR *dfd;
    char name[PATH_MAX];
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
    if(S_ISREG(fileStat.st_mode)){
        //是文件的情况
        int fd=open(transFile.c_str(),O_RDONLY);
        if(fd<0)
        {
            printf("open file failed");
            return -1;
        }
        char buf[2048]={};
        
        std::string headStr;
        std::stringstream headStrStream;
        headStrStream<<std::string("file:")<<fileName<<";"<<"size:"<<fileStat.st_size<<";";
        headStr=headStrStream.str();
        //第一次的时候传一个2048大小的头部
        strcpy(buf, headStr.c_str());
        write(sock,buf,2048);
        int readwordnum=0;
        while((readwordnum=read(fd,buf,2048))>0)
        {
            write(sock, buf, readwordnum);
        }
        close(fd);
        //文件情况结束
    }
    else if(S_ISDIR(fileStat.st_mode)){
        char buf[2048]={};
        std::string headStr;
        std::stringstream headStrStream;
        headStrStream<<std::string("document:")<<fileName<<";";
        headStr=headStrStream.str();
        strcpy(buf, headStr.c_str());
        write(sock,buf,2048);
    }
    return 0;
}

int main(int argc,char *argv[])
{
    if(argc!=4)
    {
        std::cout<<"first arg is ip\n"
        <<"second arg is port\n"
        <<"third arg is path download the file\n";
        exit(1);
    }
    std::string remoteIpAddress=argv[1];
    std::string remotePort=argv[2];
    downloadPath=argv[3];
    
    int connectSock=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in remoteSockaddr={};
    remoteSockaddr.sin_family=AF_INET;
    remoteSockaddr.sin_port=htons(atoi(remotePort.c_str()));
    remoteSockaddr.sin_addr.s_addr=inet_addr(remoteIpAddress.c_str());
    socklen_t len=sizeof(remoteSockaddr);
    if(connect(connectSock, (struct sockaddr*)&remoteSockaddr, len)<0)
    {
        perror("connect");
        exit(1);
    }
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
        //判断文件类型
        struct stat fileStat={};
        if(lstat(localFileName.c_str(), &fileStat)<0)
        {
            perror("file not exist");
            return 1;
        }
        if(S_ISREG(fileStat.st_mode))
        {
            transmitFile(connectSock,localFileName,remoteFileName);
        }
        else if(S_ISDIR(fileStat.st_mode))
        {
            //改为只传递文件名和大小
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
    return 0;
}
