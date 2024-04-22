#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <poll.h>
#include <fstream>
#include <sstream>
#include <memory>
#include <regex>
#include <csignal>
#include <cstdlib>
#include "server.hpp"

using namespace std;

vector<ClientBase*> clients;
vector<MessageInfo> sentMessages;
int retries = 3;
int counter = 250;
char* listen_ip = strdup("0.0.0.0");
uint16_t port = 4567;
int tcpServerSock, udpServerSock;

pollfd fds[1000]; // max 10 clients for now
int num_fds = 2;

void signalHandler(int signum) {
    if (signum == SIGINT) {

        for (auto client : clients) {
            delete client;
        }
        clients.clear();

        for (int i = 0; i < num_fds; ++i) {
            close(fds[i].fd);
        }

        sentMessages.clear();
        free(listen_ip);
        close(tcpServerSock);
        close(udpServerSock);

        exit(signum);
    }
}

bool isUserLoggedIn(const string& username, const ClientBase* currentClient) {
    for (const auto& client : clients) {
        if (client != currentClient && client->getUsername() == username) {
            return true;
        }
    }
    return false; 
}

bool checkUser(const string& username, const string& secret) {
    ifstream file("users.csv");
    string line;
    
    while (getline(file, line)) {
        
        // Přeskočit řádky začínající znakem #
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        istringstream iss(line);
        string user, pass, displayName;
        
        if (getline(iss, user, ',') && 
            getline(iss, pass, ',') && 
            getline(iss, displayName, ',')) {
            
            if (user == username && pass == secret) {
                return true; // User found
            }
        }
    }
    
    return false; // User not found
}


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

    char buffer[1500];
    ssize_t bytesRead = recv(clientSock, buffer, sizeof(buffer), 0);
    if (bytesRead < 0) {
        cerr << "Error receiving message from TCP client" << endl;
        close(clientSock);
        return;
    }

    TCPClient* tcpClient = new TCPClient("username", "displayName", "prevDisplayName", "NONAUTHORIZED", clientSock, clientIP, clientPort);

    string receivedData = string(buffer, bytesRead);
    size_t start = 0;
    size_t pos = receivedData.find("\r\n");

    clients.push_back(tcpClient);

    fds[num_fds].fd = clientSock;
    fds[num_fds].events = POLLIN;
    num_fds++;

    while (pos != string::npos) {
        string authMessage = receivedData.substr(start, pos - start + 2);  // Include "\r\n"
        tcpClient->handleTCPClient(tcpClient, authMessage.c_str());

        start = pos + 2;
        pos = receivedData.find("\r\n", start);
    }

    // Store the remaining data in the client's buffer
    string remainingMessage = receivedData.substr(start);
    tcpClient->setReceivedBuffer(tcpClient->getReceivedBuffer() + remainingMessage);
}


void handleUDP(int serverSock, pollfd* fds, int& num_fds) {
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
    } 
    else {
        //cout << "nový" << endl;
            
        fds[num_fds].fd = serverSock;
        fds[num_fds].events = POLLIN;
        num_fds++;
        
        UDPClient* udpClient = new UDPClient("username", "displayName", "prevDisplayName", "NONAUTHORIZED", clientIP, clientPort, serverSock, 0, retries);
        
        // Set client address
        udpClient->setClientAddr(clientAddr);
        clients.push_back(udpClient);
        udpClient->handleUDPClient(udpClient, clientBuffer);
    }
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serverAddr;
    int opt;
    bool helpRequested = false;
    while ((opt = getopt(argc, argv, "l:p:d:r:h")) != -1) {
        int parsedPort;
        switch (opt) {
            case 'l':
                listen_ip = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                for (size_t i = 0; optarg[i] != '\0'; ++i) {
                    if (!isdigit(optarg[i])) {
                        cerr << "Invalid parameter: " << optarg << ". Please provide a valid numerical value." << endl;
                        exit(-1);
                    }
                }
                parsedPort = atoi(optarg);
                if (parsedPort < 0 || parsedPort > UINT16_MAX) {
                    cerr << "Invalid time number. Please provide a valid uint16_t value." << endl;
                    exit(-1);
                }
                counter = static_cast<uint16_t>(parsedPort);
                break;
            case 'r':
                for (size_t i = 0; optarg[i] != '\0'; ++i) {
                    if (!isdigit(optarg[i])) {
                        cerr << "Invalid parameter: " << optarg << ". Please provide a valid numerical value." << endl;
                        exit(-1);
                    }
                }
                parsedPort = atoi(optarg);
                if (parsedPort < 0 || parsedPort > UINT8_MAX) {
                    cerr << "Invalid retries number. Please provide a valid uint8_t value." << endl;
                    exit(-1);
                }
                retries = static_cast<uint8_t>(parsedPort);
                break;
            case 'h':
                helpRequested = true;
                break;
            default:
                cerr << "For help use -h" << endl;
                exit(EXIT_FAILURE);
        }
    }

    if (helpRequested) {
        cout << "Usage: ./ipk24chat-server -l [server address] -p [port number] -d [timer] -r [retries]" << endl;
        cout << "   Optional parameters:" << endl;
        cout << "     - `-p port`: port number (uint16, default value 4567)" << endl;
        cout << "     - `-d timer`: timer (uint16, default value 250 ms)" << endl;
        cout << "     - `-r retries`: number of retries (uint8, default value 3)" << endl;
        cout << "     - `-h`: help" << endl;
        cout << endl;
        cout << "To close server use ctr+c to correct deallocate memmory" << endl;
        exit(0);
    }

    signal(SIGINT, signalHandler);

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
    serverAddr.sin_addr.s_addr = inet_addr(listen_ip);
    serverAddr.sin_port = htons(port);

    if (bind(tcpServerSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Error binding TCP socket" << endl;
        close(tcpServerSock);
        return -1;
    }

    if (listen(tcpServerSock, SOMAXCONN) < 0) {
        cerr << "Error listening on TCP socket: " << strerror(errno) << endl;
        close(tcpServerSock);
        return -1;
    }

    cout << "TCP server listening on port " << port << " on IP address " << inet_ntoa(serverAddr.sin_addr) << endl;

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

    cout << "UDP server listening on port " << port << " on IP address " << inet_ntoa(serverAddr.sin_addr) << endl;
    
    fds[0].fd = tcpServerSock;
    fds[0].events = POLLIN;
    fds[1].fd = udpServerSock;
    fds[1].events = POLLIN;

    while (true) {
        int ret = poll(fds, num_fds, 1);
        if (ret == -1) {
            cerr << "poll() failed" << endl;
            break;
        }
        if (fds[1].revents & POLLIN) {
            handleUDP(udpServerSock, fds, num_fds);
        }
        if (fds[0].revents & POLLIN) {
            handleNewTCPClient(tcpServerSock, fds, num_fds);
        }
        for (int i = 0; i < num_fds; ++i) {
            // Checking for incoming data
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd != udpServerSock && fds[i].fd != tcpServerSock) {
                    char buffer[1500];
                    ssize_t bytesRead = recv(fds[i].fd, buffer, sizeof(buffer), 0);
                    buffer[bytesRead] = '\0';
                    if (bytesRead == -1) {
                        cerr << "Error reading from client socket" << endl;
                    } else if (bytesRead == 0) {
                        //cout << "Client disconnected" << endl;
                        close(fds[i].fd);
                        fds[i] = fds[num_fds - 1];
                        num_fds--;

                        // Removing the disconnected client from the clients vector
                        auto disconnectedClient = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
                            TCPClient* tcpClient = dynamic_cast<TCPClient*>(client);
                            return tcpClient && tcpClient->getSocket() == fds[i].fd;
                        });

                        if (disconnectedClient != clients.end()) {
                            // Notify all clients in the same group
                            for (auto& c : clients) {
                                TCPClient* tcpClient = dynamic_cast<TCPClient*>(c);
                                if (tcpClient && tcpClient != *disconnectedClient && tcpClient->getChannelID() == (*disconnectedClient)->getChannelID()) {
                                    string messageToSend = "MSG FROM server IS " + (*disconnectedClient)->getDisplayName() + " leaved channel\r";
                                    tcpClient->sendMessage(tcpClient->getSocket(), messageToSend);
                                }
                            }

                            for (auto& c : clients) {
                                UDPClient* udpClient = dynamic_cast<UDPClient*>(c);
                                if (udpClient && udpClient->getChannelID() == (*disconnectedClient)->getChannelID()) {
                                    udpClient->sendMessage("server", (*disconnectedClient)->getDisplayName() + " leaved channel");
                                }
                            }

                            delete *disconnectedClient;  // Freeing the memory
                            clients.erase(disconnectedClient);  // Removing from the vector
                        }
                    } else {
                        auto client = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
                            TCPClient* tcpClient = dynamic_cast<TCPClient*>(client);
                            return tcpClient && tcpClient->getSocket() == fds[i].fd;
                        });

                        if (client != clients.end()) {
                            TCPClient* tcpClient = dynamic_cast<TCPClient*>(*client);
                            if (tcpClient) {
                                tcpClient->setReceivedBuffer(tcpClient->getReceivedBuffer() + string(buffer, bytesRead));
                                size_t pos;
                                while ((pos = tcpClient->getReceivedBuffer().find("\r\n")) != string::npos) {
                                    string message = tcpClient->getReceivedBuffer().substr(0, pos + 2); 
                                    tcpClient->handleTCPClient(*client, message.c_str());
                                    tcpClient->setReceivedBuffer(tcpClient->getReceivedBuffer().substr(pos + 2));
                                }
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
            //cout << "Time difference: " << timeDiff.count() << " ms" << endl;

            if (timeDiff.count() > counter) { // set later
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
                        //cout << "Sending message to client with username: ";
                        UDPClient* udpClient = dynamic_cast<UDPClient*>(*clientUDP);
                        if (udpClient) {
                            //cout << udpClient->getUsername() << endl;
                            udpClient->sendAgain(msg.content);
                            msg.retries--;
                            msg.timer = std::chrono::steady_clock::now();
                        } else {
                            cerr << "Chyba při přetypování klienta na UDPClient" << endl;
                        }
                    } else {
                        cerr << "Klient nenalezen" << endl;
                        it = sentMessages.erase(it);
                        continue;
                    }
                } else {
                    // Find the disconnected client in the clients vector
                    auto disconnectedClient = std::find_if(clients.begin(), clients.end(), [&](ClientBase* client) {
                        UDPClient* udpClient = dynamic_cast<UDPClient*>(client);
                        return udpClient && udpClient->getIP() == udpClient->getIP() && udpClient->getPort() == udpClient->getPort();
                    });

                    if (disconnectedClient != clients.end()) {
                        delete *disconnectedClient;  // Freeing the memory
                        clients.erase(disconnectedClient);  // Removing from the vector
                    }

                    // removing message
                    it = sentMessages.erase(it);
                    continue; // Continue to the next iteration to avoid incrementing the iterator
                }
            }
            ++it;
        }

    }
    return 0;
}
