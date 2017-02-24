/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.cpp
 * Author: root
 *
 * Created on February 13, 2017, 10:55 AM
 */

#include <cstdlib>
#include <stdio.h>
#include <string.h>    //strlen
#include <stdlib.h>    //strlen
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <unistd.h>    //write
#include <pthread.h> //for threading , link with lpthread
#include <iostream>
#include <unistd.h> //sleep(sec)
#include <fstream>      // std::ifstream, std::ofstream
#include <limits>

using namespace std;

const uint32_t RECV_MAX = 2048;
const uint16_t FILE_NAME_SIZE_MAX = 120;
const uint8_t HEADER_SIZE = sizeof(uint32_t);

bool SendResponseHandler(const std::string& ,const int );


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

bool FileExists (const uint8_t* uFileName) {
    return ( access( (char*) uFileName, F_OK ) != -1 );
}

std::string MessageProcess(const std::string& strBuff)
{    
    return strBuff + " Success!";
}

bool RecvRequest(const int skClient, uint8_t* uFileName, uint32_t& uFileSize)
{
    if(!uFileName)
        return false;
    
    uint32_t uReadSize = 0;
    uint32_t uLenData = 0;
    
     // read 4 bytes (length data)
    uReadSize = recv(skClient , &uLenData , HEADER_SIZE, 0);
    if(uReadSize != HEADER_SIZE)
    {
        std::cout << "recv header failed" << endl;
        return false;
    }
    uLenData = ntohl(uLenData);
    if((uLenData > FILE_NAME_SIZE_MAX) || !uLenData)
        return false;

    // read 4bytes file zize
    uReadSize = recv(skClient , &uFileSize , sizeof(uint32_t), 0);
    if(uReadSize != sizeof(uint32_t))
    {
        std::cout << "recv file size header failed" << endl;
        return false;
    }
    uFileSize = ntohl(uFileSize);    
    
    // Read data
    uReadSize = recv(skClient , uFileName, uLenData, 0);
    if(uReadSize != uLenData)
    {
        perror("recv data failed");
        return false;
    }
    
    if(FileExists(uFileName))
    {
        std::cout << "File exists!" << endl;
        return false;
    }
            
    // response to client, accept upload file
    SendResponseHandler("OK", skClient);
    
    return true;
}

bool RecvFile(const int skClient, const std::string& strFileName, const uint32_t uFileSize)
{
    uint32_t uReadSize = 0;
    uint32_t uLenData = 0;
    uint32_t uBytes = 0;
    uint8_t uRecvBuffer[RECV_MAX + 1] = {0};
    
    std::string strFilePath = GetCurrentPath() + "/output/" + strFileName;
    std::ofstream ofsWriter(strFilePath.c_str(), std::ofstream::out);

    if(!ofsWriter.is_open())
    {
        perror("create file failed");
        return false;
    }
    
    // recv file
    uint32_t uRemainFileSize = uFileSize;
    while(uRemainFileSize > 0)
    {
        // read 4 bytes (length data)
        uReadSize = recv(skClient , &uLenData , HEADER_SIZE, 0);
        if(uReadSize != HEADER_SIZE)
        {
            std::cout << "recv header failed" << endl;
            break;
        }
        uLenData = ntohl(uLenData);
        if(!uLenData || (uLenData > uRemainFileSize))
            break;
        
        // recv data
        while(uLenData > 0)
        {
            uBytes = std::min(uLenData, RECV_MAX);        

            memset(uRecvBuffer, 0, sizeof(uRecvBuffer));
            uReadSize = recv(skClient , uRecvBuffer, uBytes, 0);
            if(uReadSize != uBytes)
            {
//                ofsWriter.clear();
                ofsWriter.close();
                perror("recv data failed");
                remove(strFilePath.c_str());
                return false;
            }
            
            //write to file
            ofsWriter.write((char*)uRecvBuffer, uBytes);
            if(ofsWriter.fail())
            {
//                ofsWriter.clear();
                ofsWriter.close();
                remove(strFilePath.c_str());
                perror("write data to file failed");
                return false;
            }
            
            uLenData -= uBytes;
            uRemainFileSize -= uBytes;
        }
        
    }
    
    ofsWriter.close();
    if(uRemainFileSize != 0)
    {
        remove(strFilePath.c_str());
        return false;
    }
    
    return true;
}

bool RecvFileHandler(const int skClient)
{
    uint8_t uFileName[FILE_NAME_SIZE_MAX + 1] = {0};
    uint32_t uFileSize = 0;
    
    if(!RecvRequest(skClient, uFileName, uFileSize))
        return false;
    
    if(!RecvFile(skClient, (char*)uFileName, uFileSize))
        return false;
    
    return true;
}

/*
 * Return:
 *      true - success
 *      false - failed
 */
bool SendResponseHandler(const std::string& str,const int skClient)
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
    uSendSize = send(skClient , uResponseMessage, uLenSend, 0);
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
    int skClient = reinterpret_cast<std::uintptr_t>(skDesc);
         
    struct timeval tvTimeout;      
    tvTimeout.tv_sec = 600;
    tvTimeout.tv_usec = 0;
    
    // Set timeout
    if (setsockopt(skClient, SOL_SOCKET, SO_RCVTIMEO, (char *)&tvTimeout, sizeof(tvTimeout)) < 0)
    {
        close(skClient); // always close and free before return
        return NULL;
    }
    
    //Receive a message from client
    while (true)
    {
        // recv msg
        if(!RecvFileHandler(skClient))
            break;
        
        std::string str = MessageProcess("Upload");
        
        // send response data
        if(!SendResponseHandler(str, skClient))
            break;
        
    }

    close(skClient);
    
    return NULL;
}


int main(int argc, char** argv) {
    int skListen , skClient , nAddrLen;
    struct sockaddr_in saServer , saClient;
    
    //Create socket
    skListen = socket(AF_INET , SOCK_STREAM , 0); //create server socket if error is returned -1
    if (skListen == -1)
    {
        std::cout << "Could not create socket" << endl;
        return 0;
    }
    std::cout << "Socket created" << endl;
     
    //Prepare the sockaddr_in structure
    saServer.sin_family = AF_INET;
    saServer.sin_addr.s_addr = INADDR_ANY;
    saServer.sin_port = htons(8888);
     
    //Bind: server require port for socket if < 0 return error
    if (bind(skListen, (struct sockaddr *)&saServer , sizeof(saServer)) < 0) 
    {
        //print the error message
        perror("bind failed. Error");
        close(skListen);
        return 0;
    }
    
    //Listen
    if (listen(skListen , 3) < 0)
    {
        close(skListen);
        return 0;
    }
    
    //Accept and incoming connection
    std::cout << "Waiting for incoming connections..." << endl;
    nAddrLen = sizeof(struct sockaddr_in);   
    while ((skClient = accept(skListen, (struct sockaddr *)&saClient, (socklen_t*)&nAddrLen)) >= 0)
    {
        std::cout << "New connection accepted" << endl;
         
        pthread_t pthHandler;

        // pointer cung chi la mot bien co gia tri
        // return 0 for success
        if( pthread_create( &pthHandler, NULL, ConnectionHandler, (void*) skClient) != 0)
        {
            perror("could not create thread");
            break;
        }
    }
     
    if(skClient < 0)
    {
        perror("accept failed: ");
    }
    
    close(skListen);
    
    return 0;
}