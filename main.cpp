#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <poll.h>
#include <sstream>
#include <memory>
#include <regex>
#include "server.hpp"

using namespace std;

const int SERVER_PORT_NUMBER = 4567;
const char* LISTEN_IP_ADDRESS = "172.24.26.130";

vector<ClientBase*> clients;
vector<MessageInfo> sentMessages;

void handleNewTCPClient(int serverSock, pollfd* fds, int& num_fds) {
    int clientSock = accept(serverSock, NULL, NULL);
    if (clientSock < 0) {
        cerr << "Error accepting TCP connection" << endl;
        return;
    }

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    if (getpeername(clientSock, (struct sockaddr*)&clientAddr, &clientAddrLen) != 0) {
        cerr << "Error getting client address" << endl;
        close(clientSock);
        return;
    }

    string clientIP = inet_ntoa(clientAddr.sin_addr);
    int clientPort = ntohs(clientAddr.sin_port);

    cout << "RECV " << clientIP << ":" << clientPort << " | "<< "AUTH" << endl;


    char buffer[1024];
    ssize_t bytesRead = recv(clientSock, buffer, sizeof(buffer), 0);
    if (bytesRead < 0) {
        cerr << "Error receiving message from TCP client" << endl;
        return;
    }

    // Zde můžete zkontrolovat, zda klient již existuje
    // Například, zda již existuje klient s daným soketem
    auto existingClient = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
        TCPClient* tcpClient = dynamic_cast<TCPClient*>(client);
        return tcpClient && tcpClient->getSocket() == clientSock;
    });

    if (existingClient == clients.end()) {
        fds[num_fds].fd = clientSock;
        fds[num_fds].events = POLLIN;
        num_fds++;
        cout << "nový" << endl;

        string receivedMessage(buffer, bytesRead);

        regex authRegex("^AUTH (.*) AS (.*) USING (.*)\r\n$");
        smatch match;
        if (regex_match(receivedMessage, match, authRegex)) {
            string username = match[1];
            string displayName = match[2];
            string secret = match[3];

            string replyMessage = "REPLY OK IS nice\r\n";
            send(clientSock, replyMessage.c_str(), replyMessage.length(), 0);
            cout << "SENT " << clientIP << ":" << clientSock <<  " | " << "REPLY" << endl;

            TCPClient* tcpClient = new TCPClient(username, displayName, "prevDisplayName", "123", clientSock, clientIP, clientPort);
            clients.push_back(tcpClient);
        }
    }
}

void handleNewUDPClient(int serverSock, pollfd* fds, int& num_fds) {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    char clientBuffer[1500];

    ssize_t bytesRead = recvfrom(serverSock, clientBuffer, sizeof(clientBuffer), 0, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (bytesRead == -1) {
        cerr << "Error receiving UDP message" << endl;
        return;
    }

    // user ip and port
    string clientIP = inet_ntoa(clientAddr.sin_addr);
    int clientPort = ntohs(clientAddr.sin_port);
    
    // checking existing client port and ip
    auto existingClient = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
        UDPClient* udpClient = dynamic_cast<UDPClient*>(client);
        return udpClient && udpClient->getIP() == clientIP && udpClient->getPort() == clientPort;
    });

    if (existingClient != clients.end()) {
        UDPClient* udpClient = dynamic_cast<UDPClient*>(*existingClient);
        if (udpClient) {
            udpClient->handleUDPClient(udpClient, clientBuffer);
        } else {
            cerr << "Chyba při přetypování klienta na UDPClient" << endl;
        }
    } else {
        cout << "RECV " << clientIP << ":" <<clientPort << " | " << "AUTH" << endl;

        cout << "nový" << endl;
            
        fds[num_fds].fd = serverSock;
        fds[num_fds].events = POLLIN;
        num_fds++;
        uint16_t messageID = (clientBuffer[1] << 8) | clientBuffer[2];
        string username;
        string displayName;
        string secret;
        size_t usernameEnd = 3;

        //find username (first)
        while (clientBuffer[usernameEnd] != '\0') {
            usernameEnd++;
        }
        username = string(clientBuffer + 3, usernameEnd - 3);

        // Find display name
        size_t displayNameStart = usernameEnd + 1;
        size_t displayNameEnd = displayNameStart;
        while (clientBuffer[displayNameEnd] != '\0') {
            displayNameEnd++;
        }
        displayName = string(clientBuffer + displayNameStart, displayNameEnd - displayNameStart);

        // Find secret
        size_t secretStart = displayNameEnd + 1;
        size_t secretEnd = secretStart;
        while (clientBuffer[secretEnd] != '\0') {
            secretEnd++;
        }
        secret = string(clientBuffer + secretStart, secretEnd - secretStart);
        
        UDPClient* udpClient = new UDPClient(username, displayName, "prevDisplayName", "123", clientIP, clientPort, serverSock, 0);
        
        // Set client address
        udpClient->setClientAddr(clientAddr);
        
        clients.push_back(udpClient);

        //send confirm and reply ok
        udpClient->sendConfirmMessage(messageID);
        string replyContent = "nice\n";
        udpClient->sendReplyMessage(messageID, true, replyContent);
    }
}

int main() {
    int tcpServerSock, udpServerSock;
    struct sockaddr_in serverAddr;

    tcpServerSock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpServerSock < 0) {
        cerr << "Error creating TCP socket" << endl;
        close(tcpServerSock);
        return -1;
    }

    // Enable SO_REUSEADDR option
    int enable = 1;
    if (setsockopt(tcpServerSock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
    {
        cerr << "setsockopt(SO_REUSEADDR) failed" << endl;
        return -1;
    }

    memset((char *)&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(LISTEN_IP_ADDRESS);
    serverAddr.sin_port = htons(SERVER_PORT_NUMBER);

    if (bind(tcpServerSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Error binding TCP socket" << endl;
        close(tcpServerSock);
        return -1;
    }

    if (listen(tcpServerSock, SOMAXCONN) < 0) {
        cerr << "Error listening on TCP socket" << endl;
        close(tcpServerSock);
        return -1;
    }

    cout << "TCP server listening on port " << SERVER_PORT_NUMBER << " on IP address " << inet_ntoa(serverAddr.sin_addr) << endl;

    udpServerSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpServerSock < 0) {
        cerr << "Error creating UDP socket" << endl;
        close(udpServerSock);
        return -1;
    }

    if (bind(udpServerSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Error binding UDP socket" << endl;
        close(udpServerSock);
        return -1;
    }

    cout << "UDP server listening on port " << SERVER_PORT_NUMBER << " on IP address " << inet_ntoa(serverAddr.sin_addr) << endl;

    struct pollfd fds[10]; // max 10 clients fornow 
    int num_fds = 2;
    
    fds[0].fd = tcpServerSock;
    fds[0].events = POLLIN;
    fds[1].fd = udpServerSock;
    fds[1].events = POLLIN;

    while (true) {
        int ret = poll(fds, num_fds, 1000);
        if (ret == -1) {
            cerr << "poll() failed" << endl;
            break;
        }
        if (fds[0].revents & POLLIN) {
            handleNewTCPClient(tcpServerSock, fds, num_fds);
        }
        if (fds[1].revents & POLLIN) {
            handleNewUDPClient(udpServerSock, fds, num_fds);
        }
        for (int i = 0; i < num_fds; ++i) {
            // shit to be changed, but somehow kinda working
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd != udpServerSock && fds[i].fd != tcpServerSock) {
                    char buffer[1024];
                    ssize_t bytesRead = recv(fds[i].fd, buffer, sizeof(buffer), 0);
                    buffer[bytesRead] = '\0';
                    if (bytesRead == -1) {
                        cerr << "Error reading from client socket" << endl;
                    } else if (bytesRead == 0) {
                        cout << "Client disconnected" << endl;
                        close(fds[i].fd);
                        fds[i] = fds[num_fds - 1];
                        num_fds--;
                    } else {
                        auto client = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
                            TCPClient* tcpClient = dynamic_cast<TCPClient*>(client);
                            return tcpClient && tcpClient->getSocket() == fds[i].fd;
                        });

                        if (client != clients.end()) {
                            TCPClient* tcpClient = dynamic_cast<TCPClient*>(*client);
                            if (tcpClient) {
                                tcpClient->handleTCPClient(*client, buffer);
                            } else {
                                cerr << "Chyba při přetypování klienta na TCPClient" << endl;
                            }
                        } else {
                            cerr << "Klient nenalezen" << endl;
                        }
                    }
                }
            }
        }
        for (auto it = sentMessages.begin(); it != sentMessages.end(); ) {
            auto& msg = *it;
            auto currentTime = std::chrono::steady_clock::now();
            auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - msg.timer);
            cout << "Time difference: " << timeDiff.count() << " ms" << endl;

            // Wait for a certain time (500 milliseconds)
            if (timeDiff.count() > 500) { // set later
                // Check if `retries` for the current message is not zero
                if (msg.retries > 0) {
                    auto clientUDP = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
                        UDPClient* udpClient = dynamic_cast<UDPClient*>(client);
                        if (udpClient) {
                            return udpClient->getUsername() == msg.username;
                        }
                        return false;
                    });

                    if (clientUDP != clients.end()) {
                        cout << "Sending message to client with username: ";
                        UDPClient* udpClient = dynamic_cast<UDPClient*>(*clientUDP);
                        if (udpClient) {
                            cout << udpClient->getUsername() << endl;
                            udpClient->sendAgain(msg.content);
                            msg.retries--;
                            msg.timer = std::chrono::steady_clock::now();
                        } else {
                            cerr << "Chyba při přetypování klienta na UDPClient" << endl;
                        }
                    } else {
                        cerr << "Klient nenalezen" << endl;
                    }
                }
                else {
                    // removing message
                    it = sentMessages.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    close(tcpServerSock);
    close(udpServerSock);

    for (int i = 0; i < num_fds; ++i) {
        close(fds[i].fd);
    }
    
    // delete all clients 
    for (auto client : clients) {
        delete client;
    }

    return 0;
}
