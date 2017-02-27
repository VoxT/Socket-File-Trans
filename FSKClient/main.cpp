/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.cpp
 * Author: root
 *
 * Created on February 13, 2017, 10:57 AM
 */

#include <unistd.h>
#include <stdio.h> //printf
#include <string.h>    //strlen
#include <sys/socket.h>    //socket
#include <arpa/inet.h> //inet_addr
#include <iostream>

using namespace std;

#define READ_MAX                    2048
#define RECV_MSG_MAX                2048
#define FILE_NAME_SIZE_MAX          120
#define HEADER_SIZE                 sizeof(uint32_t)
#define HEADER_SIZE_FILE_NAME       sizeof(uint32_t)
#define HEADER_SIZE_FILE_SIZE       sizeof(uint32_t)

int g_skClient; // Client socket

std::string RecvMessageHandler();


bool RequestUploadFile(const std::string& strFileName, uint32_t uFileSize)
{
    uint32_t uSendSize = 0;
    uint32_t uLenFileName = 0;
    uint32_t uLenSend = 0;
    uint8_t uBuffer[HEADER_SIZE_FILE_NAME + HEADER_SIZE_FILE_SIZE + FILE_NAME_SIZE_MAX + 1] = {0};
    
    // file name length to header
    uLenFileName = htonl(strFileName.length());
    memcpy(uBuffer, &uLenFileName, HEADER_SIZE_FILE_NAME); // Set 4 bytes data length header
    
    // file size to header
    uint32_t uFileSizeToNet = htonl(uFileSize);
    memcpy(uBuffer + HEADER_SIZE_FILE_NAME, &uFileSizeToNet, HEADER_SIZE_FILE_SIZE); // Set 4 bytes file size header
    
    // Set file name
    memcpy(uBuffer + HEADER_SIZE_FILE_NAME + HEADER_SIZE_FILE_SIZE, strFileName.c_str(), strFileName.length()); 
    uLenSend = HEADER_SIZE_FILE_NAME + HEADER_SIZE_FILE_SIZE + strFileName.length();
    
//    cout << strlen((char*)uSendMessage) << endl; output uncorrect because of 4 bytes header
    uSendSize = send(g_skClient, uBuffer, uLenSend, 0);    
    if (uSendSize != uLenSend)
    {
        perror("send failed");
        return false;
    }
    
    // response from server for request upload file
    std::string strResponse = RecvMessageHandler();
    if(strResponse.compare("OK"))
    {
        std::cout << strResponse << endl;
        return false;
    }
    
    return true;
}

bool SendFileProccess(FILE* fReader, uint32_t uFileSize)
{
    if(!fReader)
    {
        std::cout << "open file failed" << endl;
        return false;
    }
    
    uint32_t uSendSize = 0;
    uint32_t uLenSend = 0;
    uint32_t uReadSize = 0;
    uint8_t uBuffer[READ_MAX + 1] = {0};
    
    while(uFileSize > 0)
    {
        uLenSend = std::min((uint32_t) READ_MAX, uFileSize);
        
        // read a block data
        memset(uBuffer, 0, sizeof(uBuffer));
        uReadSize = fread(uBuffer, sizeof(uint8_t), uLenSend, fReader);
        if((uReadSize != uLenSend) || ferror(fReader))
        {
            std::cout << "read file failed." << endl;
            return false;
        }
                
        // Send data
        uSendSize = send(g_skClient, uBuffer, uLenSend, 0);    
        if (uSendSize != uLenSend)
        {
            perror("send failed");
            return false;
        }
        
        uFileSize -= uLenSend;
    }
    
    return true;
}

std::string GetFileName(const std::string& strFilePath)
{
    uint16_t uPos = strFilePath.find_last_of("/");
    if(uPos == std::string::npos)
        return "";
    
    return strFilePath.substr(uPos, strFilePath.length());
}

bool SendFileHandler(const std::string& strFilePath)
{
    // Open file
    FILE* fReader = fopen(strFilePath.c_str(), "rb");
    if(!fReader)
    {
        std::cout << "open file failed" << endl;
        return false;
    }
    
    // get file size using buffer's members
    if (fseek (fReader, 0, SEEK_END) != 0)
        return false;
    int32_t nFileSize = ftell(fReader);
    if (nFileSize == -1L)
        return false;
    if (fseek (fReader, 0, SEEK_SET) != 0)
        return false;
    
    // Request upload file to server
    std::string strFileName = GetFileName(strFilePath);
    if(strFileName.empty())
        return false;
    
    if(!RequestUploadFile(strFileName, nFileSize))
    {        
        fclose(fReader);
        return false;
    }
    
    // send file
    if(!SendFileProccess(fReader, nFileSize))
    {        
        fclose(fReader);
        return false;
    }
    
    // Close file
    fclose(fReader);
    return true;
}

std::string RecvMessageHandler()
{
    uint32_t uRecvSize = 0;
    uint32_t uLenData = 0;
    uint8_t uRecvBuffer[RECV_MSG_MAX + 1] = {0};
    std::string strRecv;
    
    // Receive 4 bytes header
    uRecvSize = recv(g_skClient, &uLenData, HEADER_SIZE, 0);
    if (uRecvSize != HEADER_SIZE) 
    {
        perror("recv header failed");
        return "";
    }
    uLenData = ntohl(uLenData);
    if (!uLenData)
        return "";

    //Receive a reply from the server
    uRecvSize = recv(g_skClient, uRecvBuffer, uLenData, 0);
    if (uRecvSize != uLenData) {
        perror("recv data failed: ");
        return "";
    }

    strRecv = std::string((char*)uRecvBuffer);

    return strRecv;
}

bool Connect(const char* uIpAddress, uint16_t uPort)
{
    if(!uIpAddress)
        return false;
    
    struct sockaddr_in saServer;

    //Create socket
    g_skClient = socket(AF_INET, SOCK_STREAM, 0);
    if (g_skClient == -1) {
        std::cout << "Could not create socket" << endl;
        return false;
    }
    std::cout << "Socket created" << endl;

    saServer.sin_addr.s_addr = inet_addr(uIpAddress);
    saServer.sin_family = AF_INET;
    saServer.sin_port = htons(uPort);
    
    //Connect to remote server
    if(connect(g_skClient, (struct sockaddr *) &saServer, sizeof (saServer)) < 0) {
        perror("connect failed. Error");
        close(g_skClient);
        return false;
    }
    std::cout << "Connected" << endl;
        
    struct timeval tvTimeout;
    tvTimeout.tv_sec = 120;
    tvTimeout.tv_usec = 0;
    
    // set timeout
    if (setsockopt(g_skClient, SOL_SOCKET, SO_RCVTIMEO, (char *)&tvTimeout,sizeof(struct timeval)) < 0)
    {
        perror("set socket option failed!");
        close(g_skClient);
        return false;
    }
    
    return true;
}

/*  
 * 
 */
int main(int argc, char** argv) {
    
    uint8_t uFilePath[FILE_NAME_SIZE_MAX + 1] = {0};
    
    // connect to server
    if(!Connect("127.0.0.1", 8888))
        return 0;
    
    //keep communicating with server
    while(true) {
        std::cout << "Enter file name: ";
        std::cin.getline((char*)uFilePath, FILE_NAME_SIZE_MAX);
        
        std::string strFilePath = (char*)uFilePath;
        if(!strFilePath.compare("stop"))
            break;
        
        // Send message
        if (!SendFileHandler(strFilePath))
            break;
        
        // Receive Msg
        std::string strResponseMsg = RecvMessageHandler();
        if(strResponseMsg.empty())
            break;
        
        std::cout << "Server reply: " << strResponseMsg << endl;
    }

    close(g_skClient);
    return 0;
}
