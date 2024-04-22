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
    bool authenticated;

public:
    ClientBase(const string& username, const string& displayName, const string& prevDisplayName, const string& channelID)
        : username(username), displayName(displayName), prevDisplayName(prevDisplayName), channelID(channelID), authenticated(false) {}

    virtual ~ClientBase() {}

    string getChannelID() const {
        return channelID;
    }

    bool isAuthenticated() const {
        return authenticated;
    }

    void setAuthenticated(bool auth) {
        authenticated = auth;
    }
    string getUsername() const {
        return username;
    }
    string getDisplayName() const {
        return displayName;
    }
};


// Class for TCP client
class TCPClient : public ClientBase {
private:
    int socket;
    string ipAddress;
    int port;
    string receivedBuffer;  // Added received buffer for TCPClient

public:
    TCPClient(const string& username, const string& displayName, const string& prevDisplayName, string channelID, int socket, const string& ipAddress, int port)
        : ClientBase(username, displayName, prevDisplayName, channelID), socket(socket), ipAddress(ipAddress), port(port) {}

    int getSocket() const { return socket; }

    // Getter and setter for receivedBuffer
    const string& getReceivedBuffer() const {
        return receivedBuffer;
    }

    void setReceivedBuffer(const string& buffer) {
        receivedBuffer = buffer;
    }

    void handleTCPClient(ClientBase* client, const char* buffer);
    void handleMessage(ClientBase* client, const char* buffer);
    void handleError(ClientBase* client, const char* buffer);
    void handleJoin(ClientBase* client, const char* buffer);
    void sendMessage(int sock, const string& messageToSend);
    void handleAuth(ClientBase* client, const string &serverResponse);

    ~TCPClient() {}
};

// Class for UDP client
class UDPClient : public ClientBase {
private:
    string ipAddress;
    int port;
    int udpServerSock;
    int messageID;
    int retries;
    struct sockaddr_in clientAddr;

public:
    UDPClient(const string& username, const string& displayName, const string& prevDisplayName, string channelID, const string& ipAddress, int port, int udpServerSock, int messageID, int retries)
    : ClientBase(username, displayName, prevDisplayName, channelID), ipAddress(ipAddress), port(port), udpServerSock(udpServerSock), messageID(messageID), retries(retries) {}


    void handleUDPClient(UDPClient* udpClient,  char* buffer);
    void sendConfirmMessage(uint16_t messageID);
    void sendReplyMessage(uint16_t messageID, bool success, const string& messageContents);
    void sendMessage(const string& displayName, const string& messageContents);
    void handleJoinMessage(UDPClient* udpClient, char* buffer);
    void handleMessage(UDPClient* udpClient, char* buffer);
    void handleByeMessage(UDPClient* udpClient, char* buffer);
    void handleConfirmMessage(UDPClient* udpClient, char* buffer);
    void sendAgain(const vector<unsigned char>& message);
    void handleAuth(uint16_t messageID, UDPClient* udpClient, char* clientBuffer);
    void sendError(const string& displayName, const string& messageContents);

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

    int getRetries() const {
        return retries;
    }

    ~UDPClient() {}
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
extern bool checkUser(const string& username, const string& secret);
extern bool isUserLoggedIn(const string& username, const ClientBase* currentClient = nullptr);
extern int retries;
extern int counter;

#endif
