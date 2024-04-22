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

void TCPClient::handleTCPClient(ClientBase* client, const char* buffer) {
    string bufferStr(buffer);

    stringstream ss(bufferStr);
    string command;
    ss >> command;

    if(isAuthenticated()){
        if (command == "JOIN") {
            handleJoin(client, buffer);
            
        } else if (command == "MSG") {
            handleMessage(client, buffer);
        } else if (command == "ERR") {
            handleError(client, buffer);
        }
    } else {
        if (command == "AUTH") {
            handleAuth(client, bufferStr);
        }
    }
}

void TCPClient::handleError(ClientBase* client, const char* buffer){
    string bufferStr(buffer);

    stringstream ss(bufferStr);
    string command, from, displayName, is;
    ss >> command >> from >> displayName >> is;

    string byeMessage;
    if (command != "ERR" || from != "FROM" || displayName.empty() || is != "IS") {
        string messageToSend = "ERR FROM server IS Invalid ERR command format.\r";
        sendMessage(getSocket(), messageToSend);
        byeMessage = "BYE\r";
        sendMessage(getSocket(), byeMessage);
        return;
    }

    byeMessage = "BYE\r";    
    sendMessage(getSocket(), byeMessage);
}

void TCPClient::handleMessage(ClientBase* client, const char* buffer){
    string bufferStr(buffer);

    stringstream ss(bufferStr);
    string command, from, displayName, is;
    ss >> command >> from >> displayName >> is;

    if (command != "MSG" || from != "FROM" || displayName.empty() || is != "IS") {
        string messageToSend = "ERR FROM server IS Invalid MSG command format.\r";
        sendMessage(getSocket(), messageToSend);
        string byeMessage = "BYE\r";
        sendMessage(getSocket(), byeMessage);
        return;
    }

    size_t firstSpacePos = bufferStr.find_first_of(" \t\r\n");
    size_t secondSpacePos = bufferStr.find_first_of(" \t\r\n", firstSpacePos + 1);
    size_t thirdSpacePos = bufferStr.find_first_of(" \t\r\n", secondSpacePos + 1);
    size_t fourthSpacePos = bufferStr.find_first_of(" \t\r\n", thirdSpacePos + 1);

    if (fourthSpacePos != string::npos && fourthSpacePos + 1 < bufferStr.length()) {
        string content = bufferStr.substr(fourthSpacePos + 1, bufferStr.length() - fourthSpacePos - 3);

        regex contentRegex("[\\x21-\\x7E ]{1,1400}");

        if (!regex_match(content, contentRegex)) {
            cerr << "Invalid content format." << endl;
            string messageToSend = "ERR FROM server IS Invalid content format.\r";
            sendMessage(getSocket(), messageToSend);
            string byeMessage = "BYE\r";
            sendMessage(getSocket(), byeMessage);
            return;
        }

        string displayName = bufferStr.substr(secondSpacePos + 1, thirdSpacePos - secondSpacePos - 1);

        if (displayName != this->displayName) {
            prevDisplayName = this->displayName;
            this->displayName = displayName;
        }

        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "MSG " << content << endl;

        for (auto& c : clients) {
            TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
            if (tcpClient && tcpClient != this && tcpClient->getChannelID() == this->getChannelID()) {
                string messageToSend = "MSG FROM " + this->displayName + " IS " + content + "\r";
                cout << "SENT " << ipAddress << ":" << port <<  " | " << "MSG " << content << endl;
                tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
            }
        }

        for (auto& c : clients) {
            UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
            if (udpClient && udpClient->getChannelID() == this->getChannelID()) {
                udpClient->sendMessage(this->displayName, content);
            }
        }
    }
}

void TCPClient::handleJoin(ClientBase* client, const char* buffer) {
    string bufferStr(buffer);
    stringstream ss(bufferStr);
    string command, channelID, as, dName;
    ss >> command >> channelID >> as >> dName;

    regex channelIDRegex("[A-Za-z0-9\\-]{1,20}");

    if (command != "JOIN" || !regex_match(channelID, channelIDRegex) || as != "AS" || dName.empty()) {
        cerr << "Invalid JOIN command format." << endl;
        string messageToSend = "ERR FROM server IS Invalid JOIN command format.\r";
        sendMessage(getSocket(), messageToSend);
        string byeMessage = "BYE\r";
        sendMessage(getSocket(), byeMessage);
        return;
    }

    cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "JOIN " << channelID << endl;
    
    int sock = getSocket();

    string replyMessage = "REPLY OK IS Join success.\r\n";
    cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " <<  "REPLY" << endl;
    send(sock, replyMessage.c_str(), replyMessage.length(), 0);

    for (auto& c : clients) {
        TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
        if (tcpClient && tcpClient != this && tcpClient->getChannelID() == this->getChannelID()) {
            string messageToSend = "MSG FROM server IS " + this->displayName + " has left " + tcpClient->getChannelID() + "\r";
            cout << "SENT " << ipAddress << ":" << port <<  " | " << "MSG " << this->displayName << " has left " << tcpClient->getChannelID() << endl;
            tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
        }
    }

    for (auto& c : clients) {
        UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
        if (udpClient && udpClient->getChannelID() == this->getChannelID()) {
            udpClient->sendMessage("server", displayName + " has left " + udpClient->getChannelID());
        }
    }

    this->channelID = channelID;
    
    if (dName != this->displayName) {
        prevDisplayName = this->displayName;
        this->displayName = dName;
    }

    for (auto& c : clients) {
        TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
        if (tcpClient && tcpClient != this && tcpClient->getChannelID() == this->getChannelID()) {
            string messageToSend = "MSG FROM server IS " + this->displayName + " has joined " + tcpClient->getChannelID() + "\r";
            cout << "SENT " << ipAddress << ":" << port <<  " | " << "MSG " << this->displayName << " has joined " << tcpClient->getChannelID() << endl;
            tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
        }
    }

    for (auto& c : clients) {
        UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
        if (udpClient && udpClient->getChannelID() == this->getChannelID()) {
            udpClient->sendMessage("server", displayName + " has joined " + udpClient->getChannelID());
        }
    }
}

void TCPClient::sendMessage(int sock, const string& messageToSend){
    ssize_t bytesSent = send(sock, messageToSend.c_str(), messageToSend.length(), 0);
    if (bytesSent == -1) {
        cerr << "Error sending message" << endl;
    }
}

void TCPClient::handleAuth(ClientBase* client, const string &serverResponse){
    cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "AUTH" << endl;
    regex authRegex("^AUTH ([A-Za-z0-9\\-]{1,20}) AS ([\\x21-\\x7E]{1,20}) USING ([A-Za-z0-9\\-]{1,128})\r\n$");
    smatch match;

    if (regex_match(serverResponse, match, authRegex)) {

        string username = match[1];
        string displayName = match[2];
        string secret = match[3];
        string replyMessage;

        if (checkUser(username, secret) && !isUserLoggedIn(username, this)) {
            cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " <<  "REPLY" << endl;
            replyMessage = "REPLY OK IS Auth success.\r\n";
            sendMessage(socket, replyMessage);
            this->username = username;
            this->displayName = displayName;
            this->channelID = "default";
            this->setAuthenticated(true);

            for (auto& c : clients) {
                TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
                if (tcpClient && tcpClient != this && tcpClient->getChannelID() == this->getChannelID()) {
                    string messageToSend = "MSG FROM server IS " + this->displayName + " has joined " + tcpClient->getChannelID() + "\r";
                    cout << "SENT " << ipAddress << ":" << port <<  " | " << "MSG " << this->displayName << " has joined " << tcpClient->getChannelID() << endl;
                    tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
                }
            }

            for (auto& c : clients) {
                UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
                if (udpClient && udpClient->getChannelID() == this->getChannelID()) {
                    udpClient->sendMessage("server", displayName + " has joined " + udpClient->getChannelID());
                }
            }
        }
        else {
            if (!isUserLoggedIn(username, this)) {
                replyMessage = "REPLY NOK IS unregistered username\r\n";
                cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " <<  "!REPLY" << endl;
            }
            else {
                replyMessage = "REPLY NOK IS Someone is already using this username\r\n";
                cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " <<  "!REPLY" << endl;
            }
            sendMessage(socket, replyMessage);
        }
    } else {
        string messageToSend = "ERR FROM server IS Invalid AUTH command format.\r";
        sendMessage(getSocket(), messageToSend);
        string byeMessage = "BYE\r";
        sendMessage(getSocket(), byeMessage);
    }
}
