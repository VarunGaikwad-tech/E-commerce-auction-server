#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <string>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

const char* SERVER_IP = "127.0.0.1"; //localhost
const int PORT = 8080; // Network port to connect server and client.


// Shared state between threads
bool logged_in = false; //tracks if the client has successfully logged into the server.
std::mutex login_mutex; //Prevents simultaneous access to logged_in from multiple threads.
std::mutex cout_mutex; //Prevents overlapping of std::cout messages from different threads.


// Listens incoming messages from server using socket(sock)
void receive_messages(SOCKET sock) {
    char buffer[1024]; //Declares (temporary storage), max size 1024 bytes.
    while (true) {
        int bytes_received = recv(sock, buffer, sizeof(buffer), 0); //0: No special flags.
        if (bytes_received <= 0) {
            {
                std::lock_guard<std::mutex> guard(cout_mutex); //If disconnected, prints a red error message
                std::cout << "\033[31m\n[CONNECTION LOST] Disconnected from server\033[0m" << std::endl;
            }
            exit(1);
        }
        
        buffer[bytes_received] = '\0';
        std::string response(buffer); //Converts the received C-string into std::string object.


        // Update login state
        if (response.find("LOGIN_SUCCESS") != std::string::npos) {
            std::lock_guard<std::mutex> guard(login_mutex);
            logged_in = true; //Checks if the server sent LOGIN_SUCCESS. 
            //If yes: Locks the login_mutex to safely update logged_in to true.
        }

        {
            std::lock_guard<std::mutex> guard(cout_mutex); //locks for thread-safe printing.
            std::cout << "\r\033[2K";  // Clear the current terminal line

            // Color handling - Match server's ASCII box format
            if (response.find("BID_UPDATE") != std::string::npos) {
                std::cout << "\033[33m" << response << "\033[0m" << std::endl; //yellow
            } 
            else if (response.find("AUCTION CLOSED") != std::string::npos) { // Changed from "WINNER"
                std::cout << "\033[35m" << response << "\033[0m" << std::endl; //purple
            } 
            else if (response.find("NEXT ITEM") != std::string::npos) {
                std::cout << "\033[36m" << response << "\033[0m" << std::endl; //cyan
            } 
            else if (response.find("SUCCESS") != std::string::npos) {
                std::cout << "\033[32m" << response << "\033[0m" << std::endl; //green
            } 
            else if (response.find("REJECTED") != std::string::npos || 
                    response.find("ERROR") != std::string::npos) {
                std::cout << "\033[31m" << response << "\033[0m" << std::endl; //red
            }
            else if (response.find("[ACTIVE AUCTION ITEM]") != std::string::npos) {
                std::cout << "\033[36m" << response << "\033[0m" << std::endl; //cyan
            } 
            // Removed Unicode box check
            else {
                std::cout << response << std::endl; // No color reset needed here
            }
        }
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed: " << WSAGetLastError() << std::endl;
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::thread(receive_messages, sock).detach();

    std::string command;
    while (true) {
        {
            std::lock_guard<std::mutex> guard(cout_mutex);
            std::cout << "Enter command (LOGIN/BID): ";
        }
        std::getline(std::cin, command);

        // Validate command format
        {
            std::lock_guard<std::mutex> guard(login_mutex);
            if (command.rfind("BID", 0) == 0 && !logged_in) {
                std::cout << "ERROR: You must login first!" << std::endl;
                continue;
            }
        }

        send(sock, command.c_str(), command.size(), 0);
    }
    
    closesocket(sock);
    WSACleanup();
    return 0;
}