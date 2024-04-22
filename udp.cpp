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
    uint16_t messageID = (buffer[1] << 8) | buffer[2];

    // handle message with same messageID, (only send confirm)..
    if(messageType != 0x00){
        for (auto it = sentMessages.begin(); it != sentMessages.end(); ) {
            auto& msg = *it;
            auto clientUDP = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
                UDPClient* udpClient = dynamic_cast<UDPClient*>(client);
                if (udpClient) {
                    return udpClient->getUsername() == msg.username && messageID == msg.messageID;
                }
                return false;
            });

            if (clientUDP != clients.end()) {
                sendConfirmMessage(messageID);
                return;
            }
            ++it;
        }
    }
    //checking 
    if(isAuthenticated()){
        switch (messageType) {
        case 0x00:
            cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "CONFIRM" << endl;
            handleConfirmMessage(udpClient, buffer);
            break;
        case 0x03:
            handleJoinMessage(udpClient, buffer);
            break;
        case 0x04:
            handleMessage(udpClient, buffer);
            break;
        case 0xFF:
            cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "BYE" << endl;
            sendConfirmMessage(messageID);
            handleByeMessage(udpClient, buffer);
            break;
        case 0xFE:
            cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
            sendConfirmMessage(messageID);
            handleByeMessage(udpClient, buffer);
            break;
        default:
            cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "Authorized unknown command" << endl;
            break;
        }
    } else {
        switch (messageType) {
        case 0x00:
            cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "CONFIRM" << endl;
            handleConfirmMessage(udpClient, buffer);
            break;
        case 0x02:
            handleAuth(messageID, udpClient, buffer);
            break;
        default:
            cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "Nonauthorized unknown command" << endl;
            break;
        }   
    }
}

void UDPClient::handleAuth(uint16_t messageID, UDPClient* udpClient, char* clientBuffer) {
    cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "AUTH" << endl;
    udpClient->sendConfirmMessage(messageID);
    string username;
    string displayName;
    string secret;
    size_t usernameEnd = 3;

    while (clientBuffer[usernameEnd] != '\0') {
        usernameEnd++;
    }
    username = string(clientBuffer + 3, usernameEnd - 3);

    size_t secretStart = usernameEnd + 1;
    size_t secretEnd = secretStart;
    while (clientBuffer[secretEnd] != '\0') {
        secretEnd++;
    }
    secret = string(clientBuffer + secretStart, secretEnd - secretStart);

    size_t displayNameStart = secretEnd + 1;
    size_t displayNameEnd = displayNameStart;
    while (clientBuffer[displayNameEnd] != '\0') {
        displayNameEnd++;
    }
    displayName = string(clientBuffer + displayNameStart, displayNameEnd - displayNameStart);

    regex usernameRegex("[A-Za-z0-9\\-]{1,20}");
    regex secretRegex("[A-Za-z0-9\\-]{1,128}");
    regex displayNameRegex("[\\x21-\\x7E]{1,20}");

    smatch match;

    if (!regex_match(username, usernameRegex)) {
        cerr << "Invalid username format: " << endl;
        string messageContent = "Invalid username format.";
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
        sendError("server", messageContent);
        handleByeMessage(udpClient, clientBuffer);
        return;
    }

    if (!regex_match(displayName, displayNameRegex)) {
        cerr << "Invalid displayName format." << endl;
        string messageContent = "Invalid displayName format.";
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
        sendError("server", messageContent);
        handleByeMessage(udpClient, clientBuffer);
        return;
    }

    if (!regex_match(secret, secretRegex)) {
        cerr << "Invalid secret format." << endl;
        string messageContent = "Invalid secret format.";
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
        sendError("server", messageContent);
        handleByeMessage(udpClient, clientBuffer);
        return;
    }

    string replyContent;
    if(checkUser(username, secret) && !isUserLoggedIn(username, this)) {
        this->username = username;
        this->displayName = displayName;
        this->channelID = "default";
        this->setAuthenticated(true);
        string replyContent = "Auth success.\n";
        sendReplyMessage(messageID, true, replyContent);

        for (auto& c : clients) {
            TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
            if (tcpClient && tcpClient->getChannelID() == this->getChannelID()) {
                string messageToSend = "MSG FROM server IS " + this->displayName + " has joined " + tcpClient->getChannelID() + "\r";
                cout << "SENT " << ipAddress << ":" << port <<  " | " << "MSG " << this->displayName << " has joined " << tcpClient->getChannelID() << endl;
                tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
            }
        }

        for (auto& c : clients) {
            UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
            if (udpClient && udpClient != this && udpClient->getChannelID() == this->getChannelID()) {
                udpClient->sendMessage("server", displayName + " has joined " + udpClient->getChannelID());
            }
        }
    } else {
        if(!isUserLoggedIn(username, this)){
            replyContent = "Auth failure.\n";
        }
        else{
            replyContent = "Someone is already using this username\r\n";
        }
        sendReplyMessage(messageID, false, replyContent);
    }
}

void UDPClient::handleConfirmMessage(UDPClient* udpClient, char* buffer){
    uint16_t refMessageID = (buffer[1] << 8) | buffer[2];
    
    auto it = std::find_if(sentMessages.begin(), sentMessages.end(), [&](const MessageInfo& msg) {
        return msg.messageID == refMessageID && msg.username == getUsername();
    });

    if (it != sentMessages.end()) {
        if (it->content.size() > 0 && it->content[0] == 0xFF) {
            auto disconnectedClient = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
                UDPClient* udpClient = dynamic_cast<UDPClient*>(client);
                return udpClient && udpClient->getIP() == this->ipAddress && udpClient->getPort() == this->port;
            });

            if (disconnectedClient != clients.end()) {
                for (auto& c : clients) {
                    TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
                    if (tcpClient && tcpClient != *disconnectedClient && tcpClient->getChannelID() == (*disconnectedClient)->getChannelID()) {
                        string messageToSend = "MSG FROM server IS " + (*disconnectedClient)->getDisplayName() + " has left " + (*disconnectedClient)->getChannelID() + "\r";
                        tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
                    }
                }

                for (auto& c : clients) {
                    UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
                    if (udpClient && udpClient->getChannelID() == (*disconnectedClient)->getChannelID()) {
                        udpClient->sendMessage("server", (*disconnectedClient)->getDisplayName() + " has left " + (*disconnectedClient)->getChannelID());
                    }
                }
                delete *disconnectedClient; 
                clients.erase(disconnectedClient);
            }

            sentMessages.erase(it);
        } else {
            sentMessages.erase(it);
        }
    }
}

void UDPClient::handleByeMessage(UDPClient* udpClient, char* buffer){

    vector<unsigned char> replyMessage;

    replyMessage.push_back(0xFF);

    uint16_t messageID = getMessageID();
    
    replyMessage.push_back((messageID >> 8) & 0xFF);
    replyMessage.push_back(messageID & 0xFF);

    cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "BYE" << endl;
    int bytesSent = sendto(udpServerSock, replyMessage.data(), replyMessage.size(), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto reply failed" << endl;
    }
    
    MessageInfo messageSent;
    messageSent.timer = chrono::steady_clock::now();
    messageSent.username = getUsername();
    messageSent.retries = getRetries();
    messageSent.messageID = messageID;
    messageSent.content = replyMessage;
    sentMessages.push_back(messageSent);

    incrementMessageID();
}

void UDPClient::handleJoinMessage(UDPClient* udpClient, char* buffer){
    uint16_t refMessageID = (buffer[1] << 8) | buffer[2];

    int channelIDStart = 3;
    int channelIDEnd = channelIDStart;
    while (buffer[channelIDEnd] != '\0') {
        ++channelIDEnd;
    }

    string channelID(buffer + channelIDStart, buffer + channelIDEnd);

    regex channelIDRegex("[A-Za-z0-9\\-]{1,20}");

    if (!regex_match(channelID, channelIDRegex)) {
        string messageContent = "Invalid channelID format.";
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
        sendError("server", messageContent);
        handleByeMessage(udpClient, buffer);
        return;
    }

    cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "JOIN " << channelID << endl;

    int displayNameStart = channelIDEnd + 1;
    int displayNameEnd = displayNameStart;
    while (buffer[displayNameEnd] != '\0') {
        ++displayNameEnd;
    }

    string displayName(buffer + displayNameStart, buffer + displayNameEnd);

    if (displayName != this->displayName) {      
        prevDisplayName = this->displayName;
        this->displayName = displayName;
    }

    sendConfirmMessage(refMessageID);
    string replyContent = "Join success.\n";
    sendReplyMessage(refMessageID, true, replyContent);

    for (auto& c : clients) {
        TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
        if (tcpClient && tcpClient->getChannelID() == this->getChannelID()) {
            string messageToSend = "MSG FROM server IS " + this->displayName + " has left " + tcpClient->getChannelID() + "\r";
            cout << "SENT " << ipAddress << ":" << port <<  " | " << "MSG " << this->displayName << " has left " << tcpClient->getChannelID() << endl;
            tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
        }
    }

    for (auto& c : clients) {
        UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
        if (udpClient && udpClient != this && udpClient->getChannelID() == this->getChannelID()) {
            udpClient->sendMessage("server", displayName + " has left " + udpClient->getChannelID());
        }
    }

    this->channelID = channelID;

    for (auto& c : clients) {
        TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
        if (tcpClient && tcpClient->getChannelID() == this->getChannelID()) {
            string messageToSend = "MSG FROM server IS " + this->displayName + " has joined " + tcpClient->getChannelID() + "\r";
            cout << "SENT " << ipAddress << ":" << port <<  " | " << "MSG " << this->displayName << " has joined " << tcpClient->getChannelID() << endl;
            tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
        }
    }

    for (auto& c : clients) {
        UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
        if (udpClient && udpClient != this && udpClient->getChannelID() == this->getChannelID()) {
            udpClient->sendMessage("server", displayName + " has joined " + udpClient->getChannelID());
        }
    }
}

void UDPClient::handleMessage(UDPClient* udpClient, char* buffer){
    uint16_t refMessageID = (buffer[1] << 8) | buffer[2];

    int displayNameEnd = 3;
    while (buffer[displayNameEnd] != '\0') {
        ++displayNameEnd;
    }

    string displayName(buffer + 3, buffer + displayNameEnd);

    int messageContentStart = displayNameEnd + 1;
    int messageContentEnd = messageContentStart;
    while (buffer[messageContentEnd] != '\0') {
        ++messageContentEnd;
    }

    regex contentRegex("[\\x21-\\x7E ]{1,1400}");

    string messageContent(buffer + messageContentStart, buffer + messageContentEnd);

    cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "MSG " << messageContent << endl;

    sendConfirmMessage(refMessageID);
    
    if (!regex_match(messageContent, contentRegex)) {
        string messageContent = "Invalid content format.";
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
        sendError("server", messageContent);
        handleByeMessage(udpClient, buffer);
        return;
    }

    for (auto& c : clients) {
        TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
        if (tcpClient && tcpClient->getChannelID() == this->getChannelID()) {
            string messageToSend = "MSG FROM " + displayName + " IS " + messageContent + "\r";
            cout << "SENT " << ipAddress << ":" << port <<  " | " << "MSG " << messageContent << endl;
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

void UDPClient::sendConfirmMessage(uint16_t refMessageID) {
    cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "CONFIRM" << endl;
    vector<unsigned char> confirmMessage;

    confirmMessage.push_back(0x00);

    confirmMessage.push_back((refMessageID >> 8) & 0xFF);  
    confirmMessage.push_back(refMessageID & 0xFF);

    int bytesSent = sendto(udpServerSock, confirmMessage.data(), confirmMessage.size(), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto confirm failed" << endl;
    }
}

void UDPClient::sendReplyMessage(uint16_t refMessageID, bool success, const string& messageContents) {
    vector<unsigned char> replyMessage;

    replyMessage.push_back(0x01);

    uint16_t messageID = getMessageID();
    replyMessage.push_back((messageID >> 8) & 0xFF);
    replyMessage.push_back(messageID & 0xFF);

    replyMessage.push_back(success ? 0x01 : 0x00);

    replyMessage.push_back((refMessageID >> 8) & 0xFF);
    replyMessage.push_back(refMessageID & 0xFF);

    for (char c : messageContents) {
        replyMessage.push_back(c);
    }

    replyMessage.push_back(0x00);

    int bytesSent = sendto(udpServerSock, replyMessage.data(), replyMessage.size(), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto reply failed" << endl;
    }

    if(success){
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "REPLY" << endl;
    } else {
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "!REPLY" << endl;
    }

    MessageInfo messageSent;
    messageSent.timer = chrono::steady_clock::now();
    messageSent.username = getUsername();
    messageSent.retries = getRetries();
    messageSent.messageID = messageID;
    messageSent.content = replyMessage;
    sentMessages.push_back(messageSent);

    incrementMessageID();
}

void UDPClient::sendError(const string& displayName, const string& messageContents) {
    vector<unsigned char> message;

    message.push_back(0xFE);

    uint16_t messageID = getMessageID();
    message.push_back((messageID >> 8) & 0xFF);
    message.push_back(messageID & 0xFF);

    for (char c : displayName) {
        message.push_back(c);
    }
    message.push_back(0x00);

    for (char c : messageContents) {
        message.push_back(c);
    }
    message.push_back(0x00);

    int bytesSent = sendto(udpServerSock, message.data(), message.size(), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto UDP client failed" << endl;
    }

    MessageInfo messageSent;
    messageSent.username = getUsername();
    messageSent.timer = chrono::steady_clock::now();
    messageSent.retries = getRetries();
    messageSent.messageID = messageID;
    messageSent.content = message;
    sentMessages.push_back(messageSent);

    incrementMessageID();
}

void UDPClient::sendMessage(const string& displayName, const string& messageContents) {
    cout << "SENT " << ipAddress << ":" << port <<  " | " << "MSG " << messageContents << endl;
    vector<unsigned char> message;

    message.push_back(0x04);

    uint16_t messageID = getMessageID();
    message.push_back((messageID >> 8) & 0xFF);
    message.push_back(messageID & 0xFF);

    for (char c : displayName) {
        message.push_back(c);
    }
    message.push_back(0x00);

    for (char c : messageContents) {
        message.push_back(c);
    }
    message.push_back(0x00);

    int bytesSent = sendto(udpServerSock, message.data(), message.size(), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto UDP client failed" << endl;
    }

    MessageInfo messageSent;
    messageSent.username = getUsername();
    messageSent.timer = chrono::steady_clock::now();
    messageSent.retries = getRetries();
    messageSent.messageID = messageID;
    messageSent.content = message;
    sentMessages.push_back(messageSent);

    incrementMessageID();
}

void UDPClient::sendAgain(const vector<unsigned char>& message){
    int bytesSent = sendto(udpServerSock, message.data(), message.size(), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto again failed" << endl;
    } 
}
