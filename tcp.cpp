#include "server.hpp"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <getopt.h>
#include <algorithm> 
#include <atomic>    
#include <poll.h>
#include <regex>
#include <csignal>
#include <cstdlib>
#include <vector>
#include <string>
#include <chrono>
#include <netinet/in.h>

using namespace std;

void TCPClient::handleMessage(ClientBase* client, char* buffer){
    string bufferStr(buffer);
    size_t firstSpacePos = bufferStr.find_first_of(" \t\r\n");
    size_t secondSpacePos = bufferStr.find_first_of(" \t\r\n", firstSpacePos + 1);
    size_t thirdSpacePos = bufferStr.find_first_of(" \t\r\n", secondSpacePos + 1);
    size_t fourthSpacePos = bufferStr.find_first_of(" \t\r\n", thirdSpacePos + 1);

    if (fourthSpacePos != string::npos && fourthSpacePos + 1 < bufferStr.length()) {
        string content = bufferStr.substr(fourthSpacePos + 1, bufferStr.length() - fourthSpacePos - 3);

        /*if (!content.empty() && content.back() == '\r') {
            content.pop_back(); 
        }*/

        string displayName = bufferStr.substr(secondSpacePos + 1, thirdSpacePos - secondSpacePos - 1);

        if (displayName != this->displayName) {
            cout << "DisplayName changed from " << this->displayName << " to " << displayName << endl;
            
            prevDisplayName = this->displayName;
            this->displayName = displayName;
        }

        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "MSG " << content << endl;

        for (auto& c : clients) {
            TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
            if (tcpClient && tcpClient != this && tcpClient->getChannelID() == this->getChannelID()) {
                string messageToSend = "MSG FROM " + this->displayName + " IS " + content + "\r";
                tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
            }
        }

        for (auto& c : clients) {
            UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
            if (udpClient && udpClient->getChannelID() == this->getChannelID()) {
                udpClient->sendMessage(this->displayName, content);
            }
        }
    } else {
        cout << "Invalid message format." << endl;
    }
}

void TCPClient::handleJoin(ClientBase* client, char* buffer) {
    string bufferStr(buffer);
    stringstream ss(bufferStr);
    string command, channelID, as, dName;
    ss >> command >> channelID >> as >> dName;

    int sock = getSocket();

    string replyMessage = "REPLY OK IS nice\r\n";
    send(sock, replyMessage.c_str(), replyMessage.length(), 0);
    this->channelID = channelID;
    
    if (dName != this->displayName) {
        cout << "DisplayName changed from " << this->displayName << " to " << dName << endl;
        
        prevDisplayName = this->displayName;
        this->displayName = dName;
    }
    cout << this->displayName << " joined channel " << channelID << endl;

    for (auto& c : clients) {
        TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
        if (tcpClient && tcpClient != this && tcpClient->getChannelID() == this->getChannelID()) {
            string messageToSend = "MSG FROM server IS " + this->displayName + " joined channel" + "\r";
            tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
        }
    }

    for (auto& c : clients) {
        UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
        if (udpClient && udpClient->getChannelID() == this->getChannelID()) {
            udpClient->sendMessage(this->displayName, "joined channel");
        }
    }
}

void TCPClient::sendMessage(int sock, const string& messageToSend){
    send(sock, messageToSend.c_str(), messageToSend.length(), 0);
}

void TCPClient::handleTCPClient(ClientBase* client, char* buffer) {
    // Implementation of handleTCPClient
    cout << "tcp" << endl;

    string bufferStr(buffer);

    stringstream ss(bufferStr);
    string command;
    ss >> command;

    if (command == "JOIN") {
        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "JOIN" << endl;
        handleJoin(client, buffer);
        
    } else if (command == "MSG") {
        handleMessage(client, buffer);
    }
}
