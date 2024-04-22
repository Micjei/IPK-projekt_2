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

    //cout << "MESSAGE WITH: " << messageID << " CAME." << endl;
    
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
                //cout << "something came twice" << endl;
                //cout << "Message with ID " << messageID << " already exists for client " << udpClient->getUsername() << endl;
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
            cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "autorizovany BULLSHIT" << endl;
            //cout << "Received data: " << messageType << endl;
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
            cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "neautorizovaný BULLSHIT" << endl;
            //cout << "Received data: " << messageType << endl;
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

    // Find username (first)
    while (clientBuffer[usernameEnd] != '\0') {
        usernameEnd++;
    }
    username = string(clientBuffer + 3, usernameEnd - 3);

    // Find secret
    size_t secretStart = usernameEnd + 1;
    size_t secretEnd = secretStart;
    while (clientBuffer[secretEnd] != '\0') {
        secretEnd++;
    }
    secret = string(clientBuffer + secretStart, secretEnd - secretStart);

    // Find display name
    size_t displayNameStart = secretEnd + 1;
    size_t displayNameEnd = displayNameStart;
    while (clientBuffer[displayNameEnd] != '\0') {
        displayNameEnd++;
    }
    displayName = string(clientBuffer + displayNameStart, displayNameEnd - displayNameStart);

    // Define regex patterns
    regex usernameRegex("[A-Za-z0-9\\-]{1,20}");
    regex secretRegex("[A-Za-z0-9\\-]{1,128}");
    regex displayNameRegex("[\\x21-\\x7E]{1,20}");

    smatch match;

    // Validate username
    if (!regex_match(username, usernameRegex)) {
        cerr << "Invalid username format: " << endl;
        string messageContent = "Invalid username format.";
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
        sendError("server", messageContent);
        handleByeMessage(udpClient, clientBuffer);
        return;
    }

    // Validate displayName
    if (!regex_match(displayName, displayNameRegex)) {
        cerr << "Invalid displayName format." << endl;
        string messageContent = "Invalid displayName format.";
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
        sendError("server", messageContent);
        handleByeMessage(udpClient, clientBuffer);
        return;
    }

    // Validate secret
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
        string replyContent = "Good\n";
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
            replyContent = "Not Good\n";
        }
        else{
            replyContent = "Someone is already using this username\r\n";
        }
        //cout << "Received: " << username << " " << displayName << " " << secret << endl;
        sendReplyMessage(messageID, false, replyContent);
    }
}


void UDPClient::handleConfirmMessage(UDPClient* udpClient, char* buffer){
    uint16_t refMessageID = (buffer[1] << 8) | buffer[2];
    
    // Find the message with the matching messageID and username
    auto it = std::find_if(sentMessages.begin(), sentMessages.end(), [&](const MessageInfo& msg) {
        return msg.messageID == refMessageID && msg.username == getUsername();
    });

    if (it != sentMessages.end()) {
        // Check if the message content is 'BYE' (0xFF)
        if (it->content.size() > 0 && it->content[0] == 0xFF) {
            // Find the disconnected client in the clients vector
            auto disconnectedClient = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
                UDPClient* udpClient = dynamic_cast<UDPClient*>(client);
                return udpClient && udpClient->getIP() == this->ipAddress && udpClient->getPort() == this->port;
            });

            //cout << "Message with messageID " << refMessageID << " and username " << this->username << " confirmed as BYE and removed." << endl;

            if (disconnectedClient != clients.end()) {
                delete *disconnectedClient;  // Freeing the memory
                clients.erase(disconnectedClient);  // Removing from the vector
            }

            // Erase the message from the vector
            sentMessages.erase(it);
        } else {
            // Just erase the message from the vector
            sentMessages.erase(it);
            //cout << "Message with messageID " << refMessageID << " and username " << getUsername() << " confirmed and removed." << endl;
        }
    } else {
        //cout << "No message found with messageID " << refMessageID << " and username " << getUsername() << "." << endl;
    }
}


void UDPClient::handleByeMessage(UDPClient* udpClient, char* buffer){
    //uint16_t refMessageID = (buffer[1] << 8) | buffer[2];

    vector<unsigned char> replyMessage;

    //(BYE 0xFF)
    replyMessage.push_back(0xFF);

    // Add messageID (2 bytes)
    uint16_t messageID = getMessageID();
    
    replyMessage.push_back((messageID >> 8) & 0xFF);
    replyMessage.push_back(messageID & 0xFF);

    //sendConfirmMessage(refMessageID);

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
    //cout << "increase in bye " << endl;
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

    regex channelIDRegex("[A-Za-z0-9\\-]{1,20}");

    if (!regex_match(channelID, channelIDRegex)) {
        string messageContent = "Invalid channelID format.";
        cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " << "ERR" << endl;
        sendError("server", messageContent);
        handleByeMessage(udpClient, buffer);
        return;
    }

    cout << "RECV " << this->ipAddress << ":" << this->port <<  " | " << "JOIN " << channelID << endl;
    cout << "SENT " << this->ipAddress << ":" << this->port <<  " | " <<  "REPLY" << endl;

    int displayNameStart = channelIDEnd + 1;
    int displayNameEnd = displayNameStart;
    while (buffer[displayNameEnd] != '\0') {
        ++displayNameEnd;
    }

    string displayName(buffer + displayNameStart, buffer + displayNameEnd);

    //cout << "Join message received from " << displayName << " for channel " << channelID << endl;

    if (displayName != this->displayName) {
        //cout << "DisplayName changed from " << this->displayName << " to " << displayName << endl;
        
        prevDisplayName = this->displayName;
        this->displayName = displayName;
    }

    sendConfirmMessage(refMessageID);
    string replyContent = "nice\n";
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

    regex contentRegex("[\\x21-\\x7E ]{1,1400}");
    // Extract MessageContents field
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
    //cout << displayName << ": " << messageContent << endl; 

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
    // (confirm 0x00)
    confirmMessage.push_back(0x00);
    //messageID
    confirmMessage.push_back((refMessageID >> 8) & 0xFF);  
    confirmMessage.push_back(refMessageID & 0xFF);

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
    uint16_t messageID = getMessageID();
    replyMessage.push_back((messageID >> 8) & 0xFF);
    replyMessage.push_back(messageID & 0xFF);

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
    //cout << messageID << " reply id" << endl;

    incrementMessageID();
    //cout << "increase in reply " << endl;
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

    // MessageContents
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
    //cout << "increase in error " << endl;
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

    // MessageContents
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
    //cout << "increase in msg " << endl;
}

void UDPClient::sendAgain(const vector<unsigned char>& message){
    int bytesSent = sendto(udpServerSock, message.data(), message.size(), 0, (struct sockaddr *) &clientAddr, sizeof(clientAddr));
    if (bytesSent < 0) {
        cerr << "Sendto again failed" << endl;
    } 
}
