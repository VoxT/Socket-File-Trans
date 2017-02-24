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

#include <cstdlib>
#include <unistd.h>
#include <stdio.h> //printf
#include <string.h>    //strlen
#include <sys/socket.h>    //socket
#include <arpa/inet.h> //inet_addr
#include <iostream>
#include <fstream>      // std::filebuf, std::ifstream

using namespace std;

const uint32_t READ_MAX = 2048;
const uint32_t RECV_MSG_MAX = 2048;
const uint16_t FILE_NAME_SIZE_MAX = 120;
const uint8_t HEADER_SIZE = sizeof(uint32_t);

int g_skClient; // Client socket

std::string RecvMessageHandler();



std::string GetCurrentPath() 
{
    uint8_t uCurrentPath[1024];
    if (!getcwd((char*) uCurrentPath, sizeof(uCurrentPath)))
    {
        perror("getcwd() error");
        return "";
    }
    
    return (char*) uCurrentPath;
}

bool RequestUploadFile(const std::string& strFileName, uint32_t uFileSize)
{
    uint32_t uSendSize = 0;
    uint32_t uLenData = 0;
    uint32_t uLenSend = 0;
    uint8_t uBuffer[READ_MAX + HEADER_SIZE + 1] = {0};
    
    // file name size to header
    uLenData = htonl(strFileName.length());
    memcpy(uBuffer, &uLenData, HEADER_SIZE); // Set 4 bytes data length header
    
    // file size to header
    uint32_t uFileSizeToNet = htonl(uFileSize);
    memcpy(uBuffer + HEADER_SIZE, &uFileSizeToNet, HEADER_SIZE); // Set 4 bytes file size header
    
    // Set file name
    memcpy(uBuffer + HEADER_SIZE + sizeof(uFileSize), strFileName.c_str(), strFileName.length()); 
    uLenSend = HEADER_SIZE + sizeof(uFileSize) + strFileName.length();
    
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

bool SendFileProccess(std::filebuf* fbufReader, uint32_t uFileSize)
{
    if(!fbufReader)
        return false;
    
    uint32_t uSendSize = 0;
    uint32_t uLenData = 0;
    uint32_t uLenSend = 0;
    uint32_t uBytes = 0;
    uint8_t uBuffer[READ_MAX + HEADER_SIZE + 1] = {0};
    
    while(uFileSize > 0)
    {
        uBytes = std::min(READ_MAX, uFileSize);
        
        memset(uBuffer, 0, sizeof(uBuffer) - 1);
        uLenData = htonl(uBytes);
        memcpy(uBuffer, &uLenData, HEADER_SIZE); // Set 4 bytes data length header
        
        // read a block data
        fbufReader->sgetn((char*)(uBuffer + HEADER_SIZE), uBytes);
                
        // Send data
        uLenSend = HEADER_SIZE + uBytes;
        uSendSize = send(g_skClient, uBuffer, uLenSend, 0);    
        if (uSendSize != uLenSend)
        {
            perror("send failed");
            return false;
        }
        
        uFileSize -= uBytes;
    }
    
    return true;
}

bool SendFileHandler(const std::string& strFileName)
{
    // Open file
    std::string strFilePath = GetCurrentPath() + "/input/" + strFileName;
    std::ifstream ifsFileReader;
    ifsFileReader.open(strFilePath.c_str(), std::ifstream::in);
    if(!ifsFileReader.is_open())
    {
        perror("open file failed");
        return false;
    }
    
    // get pointer to associated buffer object
    std::filebuf* fbufReader = ifsFileReader.rdbuf();
    if(!fbufReader) 
    {
        ifsFileReader.close();
        return false;
    }
    
    // get file size using buffer's members
    uint32_t uFileSize = fbufReader->pubseekoff(0, ifsFileReader.end, ifsFileReader.in);
    fbufReader->pubseekpos (0, ifsFileReader.in);
    
    // Request upload file to server
    if(!RequestUploadFile(strFileName, uFileSize))
    {        
        ifsFileReader.close();
        return false;
    }
    
    // send file
    if(!SendFileProccess(fbufReader, uFileSize))
    {        
        ifsFileReader.close();
        return false;
    }
    
    // Close file
    ifsFileReader.close();
    return true;
}

std::string RecvMessageHandler()
{
    uint32_t uRecvSize = 0;
    uint32_t uLenData = 0;
    uint8_t uBytes = 0;
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
    while(uLenData > 0)
    {
        uBytes = std::min(uLenData, RECV_MSG_MAX);
        
        memset(uRecvBuffer, 0, sizeof(uRecvBuffer));
        uRecvSize = recv(g_skClient, uRecvBuffer, uBytes, 0);
        if (uRecvSize != uBytes) {
            perror("recv data failed: ");
            return "";
        }
        
        strRecv += std::string((char*)uRecvBuffer);
        uLenData -= uBytes;
    }

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
    tvTimeout.tv_sec = 600;
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
    
    uint8_t uFileName[FILE_NAME_SIZE_MAX + 1] = {0};
    
    // connect to server
    if(!Connect("127.0.0.1", 8888))
        return 0;
    
    //keep communicating with server
    while(true) {
        std::cout << "Enter file name: ";
        std::cin.getline((char*)uFileName, FILE_NAME_SIZE_MAX);
        
        std::string strFileName = (char*)uFileName;
        if(!strFileName.compare("stop"))
            break;
        
        // Send message
        if (!SendFileHandler(strFileName))
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
