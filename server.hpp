#ifndef SERVER_HPP
#define SERVER_HPP

#include <iostream>
#include <string>
#include <vector>
#include <netinet/in.h>
#include <chrono>

using namespace std;

// Base class for common attributes
class ClientBase {
protected:
    string username;
    string displayName;
    string prevDisplayName;
    string channelID;

public:
    ClientBase(const string& username, const string& displayName, const string& prevDisplayName, string channelID)
        : username(username), displayName(displayName), prevDisplayName(prevDisplayName), channelID(channelID) {}

    virtual ~ClientBase() {}

    string getChannelID() const {
        return channelID;
    }
};

// Class for TCP client
class TCPClient : public ClientBase {
private:
    int socket;
    string ipAddress;
    int port;

public:
    TCPClient(const string& username, const string& displayName, const string& prevDisplayName, string channelID, int socket, const string& ipAddress, int port)
        : ClientBase(username, displayName, prevDisplayName, channelID), socket(socket), ipAddress(ipAddress), port(port) {}

    int getSocket() const { return socket; }
    void handleTCPClient(ClientBase* client, char* buffer);
    void handleMessage(ClientBase* client, char* buffer);
    void handleJoin(ClientBase* client, char* buffer);
    void sendMessage(int sock, const string& messageToSend);
    string getChannelID() const { return ClientBase::getChannelID(); }
};

// Class for UDP client
class UDPClient : public ClientBase {
private:
    string ipAddress;
    int port;
    int udpServerSock;
    int messageID;
    struct sockaddr_in clientAddr;

public:
    UDPClient(const string& username, const string& displayName, const string& prevDisplayName, string channelID, const string& ipAddress, int port, int udpServerSock, int messageID)
    : ClientBase(username, displayName, prevDisplayName, channelID), ipAddress(ipAddress), port(port), udpServerSock(udpServerSock), messageID(messageID) {}


    void handleUDPClient(UDPClient* udpClient,  char* buffer);
    void sendConfirmMessage(uint16_t messageID);
    void sendReplyMessage(uint16_t messageID, bool success, const string& messageContents);
    void sendMessage(const string& displayName, const string& messageContents);
    void handleJoinMessage(UDPClient* udpClient, char* buffer);
    void handleMessage(UDPClient* udpClient, char* buffer);
    void handleByeMessage(UDPClient* udpClient, char* buffer);
    void handleConfirmMessage(UDPClient* udpClient, char* buffer);
    void sendAgain(const vector<unsigned char>& message);

    string getIP() const {
        return ipAddress;
    }

    int getPort() const {
        return port;
    }

    void setClientAddr(const struct sockaddr_in& addr) { 
        clientAddr = addr;
    }

    struct sockaddr_in getClientAddr() const { 
        return clientAddr;
    }

    void incrementMessageID() {
        ++messageID;
    }

    int getMessageID() const {
        return messageID;
    }

    int getUdpServerSock() const {
        return udpServerSock;
    }

    string getUsername() const {
        return username;
    }

};
struct MessageInfo {
    string username;
    int retries; /**< Number of retries for the message */
    int messageID; /**< ID of the message */
    vector<unsigned char> content; /**< Content of the message */
    chrono::steady_clock::time_point timer; /**< Timer for the message */
};

extern vector<MessageInfo> sentMessages; /**< Vector of sent messages */
extern vector<ClientBase*> clients;

#endif
