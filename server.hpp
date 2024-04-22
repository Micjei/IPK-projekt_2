#ifndef SERVER_HPP
#define SERVER_HPP

#include <iostream>
#include <string>
#include <vector>
#include <netinet/in.h>
#include <chrono>

using namespace std;

// Descriptions were formalized using the ChatGPT tool.

// Base class for common attributes
class ClientBase {
protected:
    string username; // Username of the client.
    string displayName; // DisplayName of the client.
    string prevDisplayName; // Previous display name of the client.
    string channelID; // Clients channelID.
    bool authenticated; // Boolean value of authorized client.

public:
    /**
     * @brief Constructor for ClientBase.
     *
     * @param username Username of the client.
     * @param displayName Display name of the client.
     * @param prevDisplayName Previous display name of the client.
     * @param channelID Channel ID the client is associated with.
     */
    ClientBase(const string& username, const string& displayName, const string& prevDisplayName, const string& channelID)
        : username(username), displayName(displayName), prevDisplayName(prevDisplayName), channelID(channelID), authenticated(false) {}

    /**
     * @brief Retrieves the channel ID associated with the client.
     *
     * @return Channel ID of the client.
     */
    string getChannelID() const {
        return channelID;
    }

    /**
     * @brief Checks if the client is authenticated.
     *
     * @return True if the client is authenticated, otherwise false.
     */
    bool isAuthenticated() const {
        return authenticated;
    }

    /**
     * @brief Sets the authentication status of the client.
     *
     * @param auth Authentication status to set.
     */
    void setAuthenticated(bool auth) {
        authenticated = auth;
    }

    /**
     * @brief Retrieves the username of the client.
     *
     * @return Username of the client.
     */
    string getUsername() const {
        return username;
    }

    /**
     * @brief Retrieves the display name of the client.
     *
     * @return Display name of the client.
     */
    string getDisplayName() const {
        return displayName;
    }

    /**
     * @brief Virtual destructor for ClientBase.
     */
    virtual ~ClientBase() {}
};

/**
 * @brief Class representing a TCP client.
 * 
 * Inherits from ClientBase and adds TCP-specific functionality and attributes.
 */
class TCPClient : public ClientBase {
private:
    int socket; // Client socket.
    string ipAddress; // Client ip address.
    int port; // Client port.
    string receivedBuffer;  // Client received buffer.

public:
    /**
     * @brief Constructor for TCPClient.
     *
     * @param username Username of the client.
     * @param displayName Display name of the client.
     * @param prevDisplayName Previous display name of the client.
     * @param channelID Channel ID the client is associated with.
     * @param socket Socket file descriptor.
     * @param ipAddress IP address of the client.
     * @param port Port number of the client.
     */
    TCPClient(const string& username, const string& displayName, const string& prevDisplayName, string channelID, int socket, const string& ipAddress, int port)
        : ClientBase(username, displayName, prevDisplayName, channelID), socket(socket), ipAddress(ipAddress), port(port) {}

    /**
    * @brief Handles incoming TCP client messages.
    *
    * This function processes incoming messages from TCP clients based on their command type.
    * If the client is authenticated, it handles JOIN, MSG, and ERR commands.
    * If the client is not authenticated, it handles only the AUTH command.
    *
    * @param client Pointer to the client object.
    * @param buffer Received message buffer.
    */
    void handleTCPClient(ClientBase* client, const char* buffer);

    /**
    * @brief Handles error messages received from the TCP client.
    *
    * This function processes error messages received from the TCP client.
    * It validates the format of the error message and sends a response accordingly.
    * If the error message format is invalid, it sends an error message back to the client.
    *
    * @param client Pointer to the client object.
    * @param buffer Received message buffer containing the error message.
    */
    void handleError(ClientBase* client, const char* buffer);

    /**
    * @brief Handles incoming MESSAGE commands from the TCP client.
    *
    * This function processes incoming MESSAGE commands received from the TCP client.
    * It validates the format of the message and sends it to other clients in the same channel.
    * If the message format is invalid, it sends an error message back to the client.
    *
    * @param client Pointer to the client object.
    * @param buffer Received message buffer containing the MESSAGE command and content.
    */
    void handleMessage(ClientBase* client, const char* buffer);

    /**
    * @brief Handles incoming JOIN commands from the TCP client.
    *
    * This function processes incoming JOIN commands received from the TCP client.
    * It validates the format of the JOIN command and updates the client's channel ID and display name.
    * It also sends notifications to other clients in the same channel about the JOIN event.
    * If the JOIN command format is invalid, it sends an error message back to the client.
    *
    * @param client Pointer to the client object.
    * @param buffer Received message buffer containing the JOIN command and details.
    */
    void handleJoin(ClientBase* client, const char* buffer);

    /**
    * @brief Sends a message to the specified socket.
    *
    * Send message to TCP clients.
    * If there is an error during the send operation, it prints an error message to stderr.
    *
    * @param sock The socket descriptor to which the message will be sent.
    * @param messageToSend The message to be sent.
    */
    void sendMessage(int sock, const string& messageToSend);

    /**
    * @brief Handles authentication response from the server.
    *
    * Validates authentication command format and user credentials.
    * Updates client details upon successful authentication.
    * Sends notifications to other clients about the new user.
    *
    * @param client Pointer to the TCP client.
    * @param serverResponse Authentication response from the server.
    */
    void handleAuth(ClientBase* client, const string &serverResponse);

   /**
    * @brief Retrieves the socket associated with the TCP client.
    *
    * @return The socket file descriptor.
    */
    int getSocket() const { return socket; }

    /**
    * @brief Retrieves the received buffer from the TCP client.
    *
    * @return Constant reference to the received buffer.
    */
    const string& getReceivedBuffer() const {
        return receivedBuffer;
    }

    /**
    * @brief Sets the received buffer for the TCP client.
    *
    * @param buffer The buffer to set.
    */
    void setReceivedBuffer(const string& buffer) {
        receivedBuffer = buffer;
    }

    /**
    * @brief Destructor for the TCP client.
    *
    * Cleans up any resources associated with the TCP client.
    */
    ~TCPClient() {}
};

/**
 * @brief Class representing a UDP client.
 * 
 * Inherits from ClientBase and adds UDP-specific functionality and attributes.
 */
class UDPClient : public ClientBase {
private:
    string ipAddress; // IP address of the client.
    int port; // Port on which the client is listening.
    int udpServerSock; // UDP socket for communication. 
    int messageID; // Identifier for the last sent message by server to client. 
    int retries; // Number of retries for retransmission. 
    struct sockaddr_in clientAddr; // Structure to store client address information. 

public:
    /**
     * @brief Constructor for UDPClient class.
     *
     * @param username Username of the client.
     * @param displayName Display name of the client.
     * @param prevDisplayName Previous display name of the client.
     * @param channelID Channel ID of the client.
     * @param ipAddress IP address of the client.
     * @param port Port on which the client is listening.
     * @param udpServerSock UDP socket for communication.
     * @param messageID Identifier for the last sent message by server to client.
     * @param retries Number of retries for retransmission.
     */
    UDPClient(const string& username, const string& displayName, const string& prevDisplayName, string channelID, const string& ipAddress, int port, int udpServerSock, int messageID, int retries)
    : ClientBase(username, displayName, prevDisplayName, channelID), ipAddress(ipAddress), port(port), udpServerSock(udpServerSock), messageID(messageID), retries(retries) {}

    /**
    * @brief Handles the UDP client.
    *
    * This function processes incoming messages from the UDP client based on their message type.
    *
    * @param udpClient Pointer to the UDP client.
    * @param buffer Received message buffer.
    */
    void handleUDPClient(UDPClient* udpClient,  char* buffer);

    /**
    * @brief Handles authentication for the UDP client.
    *
    * This function processes the authentication request from the UDP client and validates the provided credentials.
    *
    * @param messageID ID of the received message.
    * @param udpClient Pointer to the UDP client.
    * @param clientBuffer Received message buffer.
    */
    void handleAuth(uint16_t messageID, UDPClient* udpClient, char* clientBuffer);

    /**
    * @brief Handles confirm message from the UDP client.
    *
    * This function processes the confirm message received from the UDP client to acknowledge the receipt
    * of a previously sent message.
    *
    * @param udpClient Pointer to the UDP client.
    * @param buffer Received message buffer.
    */
    void handleConfirmMessage(UDPClient* udpClient, char* buffer);

    /**
    * @brief Handles the BYE message from the UDP client.
    *
    * This function handles the BYE message received from the UDP client.
    * It sends a reply message to acknowledge the receipt of the BYE message.
    *
    * @param udpClient Pointer to the UDP client.
    * @param buffer Received message buffer.
    */
    void handleByeMessage(UDPClient* udpClient, char* buffer);

    /**
    * @brief Handles the JOIN message from the UDP client.
    *
    * This function handles the JOIN message received from the UDP client.
    * It validates the channelID and updates the client's display name and channelID.
    * It also sends notifications to other clients in the same channel about the JOIN event.
    *
    * @param udpClient Pointer to the UDP client.
    * @param buffer Received message buffer.
    */
    void handleJoinMessage(UDPClient* udpClient, char* buffer);

    /**
    * @brief Handles the MESSAGE message from the UDP client.
    *
    * This function handles the MESSAGE message received from the UDP client.
    * It validates the message content and sends the message to other clients in the same channel.
    *
    * @param udpClient Pointer to the UDP client.
    * @param buffer Received message buffer.
    */
    void handleMessage(UDPClient* udpClient, char* buffer);

    /**
    * @brief Sends a CONFIRM message to the UDP client.
    *
    * This function sends a CONFIRM message to the UDP client with the provided reference message ID.
    *
    * @param refMessageID Reference message ID for the CONFIRM message.
    */
    void sendConfirmMessage(uint16_t messageID);

    /**
    * @brief Sends a REPLY message to the UDP client.
    *
    * This function sends a REPLY message to the UDP client with the provided reference message ID,
    * success status, and message contents.
    *
    * @param refMessageID Reference message ID for the REPLY message.
    * @param success Success status indicating whether the operation was successful.
    * @param messageContents Contents of the message to be sent in the REPLY.
    */
    void sendReplyMessage(uint16_t messageID, bool success, const string& messageContents);

    /**
    * @brief Sends an ERROR message to the UDP client.
    *
    * This function sends an ERROR message to the UDP client with the provided display name
    * and message contents.
    *
    * @param displayName Display name associated with the error message.
    * @param messageContents Contents of the error message.
    */
    void sendError(const string& displayName, const string& messageContents);

    /**
    * @brief Sends a MESSAGE message to the UDP client.
    *
    * This function sends a MESSAGE message to the UDP client with the provided display name
    * and message contents.
    *
    * @param displayName Display name associated with the message.
    * @param messageContents Contents of the message.
    */
    void sendMessage(const string& displayName, const string& messageContents);

    /**
    * @brief Sends a message again to the UDP client.
    *
    * This function sends the provided message again to the UDP client.
    *
    * @param message The message to be sent again.
    */
    void sendAgain(const vector<unsigned char>& message);

    /**
     * @brief Gets the IP address of the client.
     *
     * @return IP address of the client.
     */
    string getIP() const {
        return ipAddress;
    }

    /**
     * @brief Gets the port on which the client is listening.
     *
     * @return Port number.
     */
    int getPort() const {
        return port;
    }

    /**
     * @brief Sets the client address.
     *
     * @param addr Client address structure.
     */
    void setClientAddr(const struct sockaddr_in& addr) { 
        clientAddr = addr;
    }

    /**
     * @brief Gets the client address structure.
     *
     * @return Client address structure.
     */
    struct sockaddr_in getClientAddr() const { 
        return clientAddr;
    }

    /**
     * @brief Increments the message ID.
     */
    void incrementMessageID() {
        ++messageID;
    }

    /**
     * @brief Gets the current message ID.
     *
     * @return Current message ID.
     */
    int getMessageID() const {
        return messageID;
    }

    /**
     * @brief Gets the UDP server socket.
     *
     * @return UDP server socket.
     */
    int getUdpServerSock() const {
        return udpServerSock;
    }

    /**
     * @brief Gets the number of retries for retransmission.
     *
     * @return Number of retries.
     */
    int getRetries() const {
        return retries;
    }

    /**
     * @brief Destructor for UDPClient class.
     */
    ~UDPClient() {}
};
struct MessageInfo {
    string username; // username of UDP client
    int retries; // Numbers of retransmission
    int messageID; // ID of the message
    vector<unsigned char> content; // Content of the message 
    chrono::steady_clock::time_point timer; // Timer for the message 
};

extern vector<MessageInfo> sentMessages; // Vector of sent messages
extern vector<ClientBase*> clients; // Vector of connected clients
extern bool checkUser(const string& username, const string& secret); // Bool function to check their "registration"
extern bool isUserLoggedIn(const string& username, const ClientBase* currentClient = nullptr); // Check connected clients for usage of username
extern int retries; // Numbers of retransmission
extern int counter; // How often retransmission

#endif
