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

void UDPClient::handleUDPClient(UDPClient* udpClient, char* buffer) {
    uint8_t messageType = buffer[0];

    //checking 
    switch (messageType) {
    case 0x00:
        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "CONFIRM" << endl;
        handleConfirmMessage(udpClient, buffer);
        break;
    case 0x01:
        //will not even use xd
        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " <<  "REPLY" << endl;
        break;
    case 0x02:
        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "AUTH" << endl;
        break;
    case 0x03:
        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "JOIN" << endl;
        handleJoinMessage(udpClient, buffer);
        break;
    case 0x04:
        handleMessage(udpClient, buffer);
        break;
    case 0xFF:
        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "BYE" << endl;
        handleByeMessage(udpClient, buffer);
        break;
    case 0xFE:
        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
        break;
    default:
        cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "BULLSHIT" << endl;
        cout << buffer << endl;
        break;
    }
}

void UDPClient::handleConfirmMessage(UDPClient* udpClient, char* buffer){
    uint16_t refMessageID = (buffer[1] << 8) | buffer[2];
    
    // Find the message with the matching messageID and username
    auto it = std::find_if(sentMessages.begin(), sentMessages.end(), [&](const MessageInfo& msg) {
        return msg.messageID == refMessageID && msg.username == this->username;
    });

    if (it != sentMessages.end()) {
        // Erase the message from the vector
        sentMessages.erase(it);
        cout << "Message with messageID " << refMessageID << " and username " << this->username << " confirmed and removed." << endl;
    } else {
        cout << "No message found with messageID " << refMessageID << " and username " << this->username << "." << endl;
    }
}

void UDPClient::handleByeMessage(UDPClient* udpClient, char* buffer){
    uint16_t refMessageID = (buffer[1] << 8) | buffer[2];

    struct sockaddr_in addr = udpClient->getClientAddr();
    vector<unsigned char> message;

    //(BYE 0xFF)
    message.push_back(0xFF);

    // Add messageID (2 bytes)
    uint16_t messageID = udpClient->getMessageID();
    
    message.push_back((messageID >> 8) & 0xFF);
    message.push_back(messageID & 0xFF);

    sendConfirmMessage(refMessageID);
    sendto(udpClient->getUdpServerSock(), message.data(), message.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
    
    MessageInfo messageSent;
    messageSent.timer = chrono::steady_clock::now();
    messageSent.username = getUsername();
    messageSent.retries = 4;
    messageSent.messageID = messageID;
    messageSent.content = message;
    sentMessages.push_back(messageSent);

    incrementMessageID();
    cout << "increase in bye " << endl;
}

void UDPClient::handleJoinMessage(UDPClient* udpClient, char* buffer){
     // Extrahování MessageID
    uint16_t refMessageID = (buffer[1] << 8) | buffer[2];

    int channelIDStart = 3;
    int channelIDEnd = channelIDStart;
    while (buffer[channelIDEnd] != '\0') {
        ++channelIDEnd;
    }

    string channelID(buffer + channelIDStart, buffer + channelIDEnd);

    int displayNameStart = channelIDEnd + 1;
    int displayNameEnd = displayNameStart;
    while (buffer[displayNameEnd] != '\0') {
        ++displayNameEnd;
    }

    string displayName(buffer + displayNameStart, buffer + displayNameEnd);

    cout << "Join message received from " << displayName << " for channel " << channelID << endl;

    if (displayName != this->displayName) {
        cout << "DisplayName changed from " << this->displayName << " to " << displayName << endl;
        
        prevDisplayName = this->displayName;
        this->displayName = displayName;
    }

    this->channelID = channelID;

    sendConfirmMessage(refMessageID);
    string replyContent = "nice\n";
    sendReplyMessage(refMessageID, true, replyContent);

    for (auto& c : clients) {
        TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
        if (tcpClient && tcpClient->getChannelID() == this->getChannelID()) {
            string messageToSend = "MSG FROM server IS " + this->displayName + " joined channel" + "\r";
            tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
        }
    }

    for (auto& c : clients) {
        UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
        if (udpClient && udpClient != this && udpClient->getChannelID() == this->getChannelID()) {
            udpClient->sendMessage(displayName, "joined channel");
        }
    }
}


void UDPClient::handleMessage(UDPClient* udpClient, char* buffer){
    uint16_t refMessageID = (buffer[1] << 8) | buffer[2];

    int displayNameEnd = 3; // Starting from index 3
    while (buffer[displayNameEnd] != '\0') {
        ++displayNameEnd;
    }

    // Extract DisplayName field
    string displayName(buffer + 3, buffer + displayNameEnd);

    // Find the end of MessageContents field
    int messageContentStart = displayNameEnd + 1;
    int messageContentEnd = messageContentStart; // Start from displayNameEnd + 1
    while (buffer[messageContentEnd] != '\0') {
        ++messageContentEnd;
    }

    // Extract MessageContents field
    string messageContent(buffer + messageContentStart, buffer + messageContentEnd);

    //cout << displayName << ": " << messageContent << endl; 

    sendConfirmMessage(refMessageID);
    cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "MSG " << messageContent << endl;

    for (auto& c : clients) {
        TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
        if (tcpClient && tcpClient->getChannelID() == this->getChannelID()) {
            string messageToSend = "MSG FROM " + displayName + " IS " + messageContent + "\r";
            tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
        }
    }

    for (auto& c : clients) {
        UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
        if (udpClient && udpClient != this && udpClient->getChannelID() == this->getChannelID()) {
            udpClient->sendMessage(displayName, messageContent);
        }
    }
}

void UDPClient::sendConfirmMessage(uint16_t messageID) {
    vector<unsigned char> confirmMessage;
    // (confirm 0x00)
    confirmMessage.push_back(0x00);
    //messageID
    confirmMessage.push_back((messageID >> 8) & 0xFF);  
    confirmMessage.push_back(messageID & 0xFF);

    int bytesSent = sendto(udpServerSock, confirmMessage.data(), confirmMessage.size(), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto confirm failed" << endl;
    }
}

void UDPClient::sendReplyMessage(uint16_t refMessageID, bool success, const string& messageContents) {
    vector<unsigned char> replyMessage;

    // REPLY
    replyMessage.push_back(0x01);

    // MessageID (2 bytes)
    replyMessage.push_back((this->messageID >> 8) & 0xFF);
    replyMessage.push_back(this->messageID & 0xFF);

    // result(1 byte)
    replyMessage.push_back(success ? 0x01 : 0x00);

    // Ref_MessageID (2 bytes)
    replyMessage.push_back((refMessageID >> 8) & 0xFF);
    replyMessage.push_back(refMessageID & 0xFF);

    // content
    for (char c : messageContents) {
        replyMessage.push_back(c);
    }

    // zero byte
    replyMessage.push_back(0x00);

    int bytesSent = sendto(udpServerSock, replyMessage.data(), replyMessage.size(), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto reply failed" << endl;
    }

    cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "REPLY" << endl;


    MessageInfo messageSent;
    messageSent.timer = chrono::steady_clock::now();
    messageSent.username = getUsername();
    messageSent.retries = 4;
    messageSent.messageID = refMessageID;
    messageSent.content = replyMessage;
    sentMessages.push_back(messageSent);
    cout << refMessageID << " reply id" << endl;

    incrementMessageID();
    cout << "increase in reply " << endl;
}

void UDPClient::sendMessage(const string& displayName, const string& messageContents) {
    vector<unsigned char> message;

    message.push_back(0x04);

    message.push_back((this->messageID >> 8) & 0xFF);
    message.push_back(this->messageID & 0xFF);

    for (char c : displayName) {
        message.push_back(c);
    }
    message.push_back(0x00);

    // MessageContents
    for (char c : messageContents) {
        message.push_back(c);
    }
    message.push_back(0x00);

    int bytesSent = sendto(udpServerSock, message.data(), message.size(), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto UDP client failed" << endl;
    }

    cout << this->messageID << " LOL " << endl;
    MessageInfo messageSent;
    messageSent.username = getUsername();
    messageSent.timer = chrono::steady_clock::now();
    messageSent.retries = 4;
    messageSent.messageID = getMessageID();
    messageSent.content = message;
    sentMessages.push_back(messageSent);

    incrementMessageID();
    cout << "increase in msg " << endl;
}

void UDPClient::sendAgain(const vector<unsigned char>& message){
    int bytesSent = sendto(udpServerSock, message.data(), message.size(), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto again failed" << endl;
    } 
}
