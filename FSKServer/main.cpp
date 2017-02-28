/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.cpp
 * Author: Thieu Vo
 *
 * Created on February 13, 2017, 10:55 AM
 */

#include <string.h>    //strlen
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <pthread.h> //for threading , link with lpthread
#include <iostream>
#include <unistd.h> //sleep(sec)

using namespace std;

#define RECV_MAX                 2048
#define FILE_NAME_SIZE_MAX       120
#define HEADER_SIZE              sizeof(uint32_t)

bool SendResponseHandler(const std::string&, int);

std::string GetCurrentPath()
{
    uint8_t uCurrentPath[1024];
    if (!getcwd((char*) uCurrentPath, sizeof (uCurrentPath) - 1))
    {
        perror("getcwd() error");
        return "";
    }

    return (char*) uCurrentPath;
}

bool FileExists(const string& strFileName)
{
    std::string strFilePath = GetCurrentPath() + "/output/" + strFileName;
    return ( access(strFilePath.c_str(), F_OK) != -1);
}

std::string MessageProcess(const std::string& strBuff)
{
    return strBuff + " Success!";
}

bool RecvRequest(int skClient, string& strFileName, uint32_t& uFileSize)
{
    strFileName.clear();
    uFileSize = 0;

    uint32_t uRecvSize = 0;
    uint32_t uLenFileName = 0;
    uint8_t uFileName[FILE_NAME_SIZE_MAX + 1] = {0};

    // read 4 bytes (length data)
    uRecvSize = recv(skClient, &uLenFileName, HEADER_SIZE, 0);
    if (uRecvSize != HEADER_SIZE)
    {
        std::cout << "recv header failed" << endl;
        return false;
    }
    uLenFileName = ntohl(uLenFileName);
    if ((uLenFileName > FILE_NAME_SIZE_MAX) || !uLenFileName)
        return false;

    // read 4bytes file zize
    uRecvSize = recv(skClient, &uFileSize, sizeof (uint32_t), 0);
    if (uRecvSize != sizeof (uint32_t))
    {
        std::cout << "recv file size header failed" << endl;
        return false;
    }
    uFileSize = ntohl(uFileSize);

    // Read data
    uRecvSize = recv(skClient, uFileName, uLenFileName, 0);
    if (uRecvSize != uLenFileName)
    {
        perror("recv data failed");
        return false;
    }

    // Check if file exists
    strFileName = std::string((char*) uFileName);
    if (FileExists(strFileName))
    {
        std::cout << "File exists!" << endl;
        SendResponseHandler("File Exists!", skClient);
        return false;
    }

    // response to client, accept upload file
    if (!SendResponseHandler("OK", skClient))
        return false;

    return true;
}

bool RecvFileData(int skClient, const std::string& strFileName, uint32_t uFileSize)
{
    if (strFileName.empty())
    {
        std::cout << "File name empty" << endl;
        return false;
    }

    uint32_t uRecvSize = 0;
    uint8_t uRecvBuffer[RECV_MAX + 1] = {0};
    uint32_t uWriteSize = 0;

    std::string strFilePath = GetCurrentPath() + "/output/" + strFileName;
    FILE* fWriter = fopen(strFilePath.c_str(), "wb");
    if (!fWriter)
    {
        std::cout << "create file failed" << endl;
        return false;
    }

    // recv file
    while (uFileSize > 0)
    {
        memset((char*) uRecvBuffer, 0, sizeof (uRecvBuffer));
        uRecvSize = recv(skClient, uRecvBuffer, RECV_MAX, 0);

        //write to file
        uWriteSize = fwrite(uRecvBuffer, sizeof (uint8_t), uRecvSize, fWriter);
        if ((uWriteSize != uRecvSize) || ferror(fWriter))
        {
            std::cout << "write data to file failed" << endl;
            break;
        }

        uFileSize -= uRecvSize;
    }

    fclose(fWriter);
    if (uFileSize != 0)
    {
        if (remove(strFilePath.c_str()) != 0)
            std::cout << "remove file error" << endl;

        return false;
    }

    return true;
}

bool RecvFileHandler(int skClient)
{
    uint32_t uFileSize = 0;
    std::string strFileName;

    if (!RecvRequest(skClient, strFileName, uFileSize))
        return false;

    if (!RecvFileData(skClient, strFileName, uFileSize))
        return false;

    return true;
}

/*
 * Return:
 *      true - success
 *      false - failed
 */
bool SendResponseHandler(const std::string& str, int skClient)
{
    uint32_t uSendSize = 0;
    uint32_t uLenData = 0;
    uint32_t uLenSend = 0;
    uint8_t uResponseMessage[RECV_MAX + 1] = {0};

    // Init response data
    uLenData = htonl(str.length());
    memcpy(uResponseMessage, &uLenData, HEADER_SIZE); // Set header
    memcpy(uResponseMessage + HEADER_SIZE, str.c_str(), str.length()); // Set data
    uLenSend = HEADER_SIZE + str.length();

    // Send data
    uSendSize = send(skClient, uResponseMessage, uLenSend, 0);
    if (uSendSize != uLenSend)
    {
        perror("send failed");
        return false;
    }

    return true;
}

/*
 * This will handle connection for each client
 * */
void *ConnectionHandler(void *skDesc)
{
    if (!skDesc)
        return NULL;

    //Get the socket descriptor
    int skClient = reinterpret_cast<std::uintptr_t> (skDesc);

    struct timeval tvTimeout;
    tvTimeout.tv_sec = 120;
    tvTimeout.tv_usec = 0;

    // Set timeout
    if (setsockopt(skClient, SOL_SOCKET, SO_RCVTIMEO, (char *) &tvTimeout, sizeof (tvTimeout)) < 0)
    {
        close(skClient); // always close and free before return
        return NULL;
    }

    //Receive a message from client
    while (true)
    {
        // recv msg
        if (!RecvFileHandler(skClient))
            break;

        std::string str = MessageProcess("Upload");

        // send response data
        if (!SendResponseHandler(str, skClient))
            break;
    }

    close(skClient);

    return NULL;
}

int CreateSocket(uint16_t uPort)
{
    int skListen;
    struct sockaddr_in saServer;

    //Create socket
    skListen = socket(AF_INET, SOCK_STREAM, 0); //create server socket if error is returned -1
    if (skListen == -1)
    {
        std::cout << "Could not create socket" << endl;
        return -1;
    }
    std::cout << "Socket created" << endl;

    //Prepare the sockaddr_in structure
    saServer.sin_family = AF_INET;
    saServer.sin_addr.s_addr = INADDR_ANY;
    saServer.sin_port = htons(uPort);

    //Bind: server require port for socket if < 0 return error
    if (bind(skListen, (struct sockaddr *) &saServer, sizeof (saServer)) < 0)
    {
        //print the error message
        perror("bind failed. Error");
        close(skListen);
        return -1;
    }

    return skListen;
}

int main(int argc, char** argv)
{
    int skListen, skClient, nAddrLen;
    struct sockaddr_in saClient;

    skListen = CreateSocket(8888);
    if (skListen == -1)
    {
        std::cout << "Could not create socket" << endl;
        return -1;
    }

    //Listen
    if (listen(skListen, 3) < 0)
    {
        close(skListen);
        return 0;
    }

    //Accept and incoming connection
    std::cout << "Waiting for incoming connections..." << endl;
    nAddrLen = sizeof (struct sockaddr_in);
    while ((skClient = accept(skListen, (struct sockaddr *) &saClient, (socklen_t*) & nAddrLen)) >= 0)
    {
        std::cout << "New connection accepted" << endl;

        pthread_t pthHandler;

        // pointer cung chi la mot bien co gia tri
        // return 0 for success
        if (pthread_create(&pthHandler, NULL, ConnectionHandler, (void*) skClient) != 0)
        {
            perror("could not create thread");
            break;
        }
    }

    if (skClient < 0)
    {
        perror("accept failed: ");
    }

    close(skListen);

    return 0;
}