#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define PORT "8080"
#define BUFFER_SIZE 1024

#define MAX_CLIENTS 10

typedef struct {
    SOCKET socket;
    char name[50];
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
CRITICAL_SECTION client_list_lock;

void broadcast_message(const char *message, SOCKET sender_socket) {
    EnterCriticalSection(&client_list_lock);
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].socket != sender_socket) {
            send(clients[i].socket, message, strlen(message), 0);
        }
    }
    LeaveCriticalSection(&client_list_lock);
}

DWORD WINAPI handle_client(void* arg) {
    SOCKET client_socket = *(SOCKET*)arg;
    char buffer[BUFFER_SIZE];
    int bytes_received;

    send(client_socket, "Enter your name: ", 17, 0);
    char client_name[50];
    bytes_received = recv(client_socket, client_name, sizeof(client_name) - 1, 0);
    if (bytes_received > 0) {
        client_name[bytes_received] = '\0';
        printf("Client %s connected.\n", client_name);
    }

    EnterCriticalSection(&client_list_lock);
    clients[client_count].socket = client_socket;
    strncpy(clients[client_count].name, client_name, sizeof(client_name) - 1);
    client_count++;
    LeaveCriticalSection(&client_list_lock);

    fd_set readfds;
    struct timeval timeout;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);

        timeout.tv_sec = 1;  
        timeout.tv_usec = 0;

        int select_result = select(0, &readfds, NULL, NULL, &timeout);
        if (select_result == SOCKET_ERROR) {
            printf("Select failed with error: %d\n", WSAGetLastError());
            break;
        }

        if (FD_ISSET(client_socket, &readfds)) {
            memset(buffer, 0, BUFFER_SIZE);
            bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);

            if (bytes_received == SOCKET_ERROR) {
                printf("Recv failed with error: %d (Socket: %d)\n", WSAGetLastError(), client_socket);
                break;
            } else if (bytes_received == 0) {
                printf("Client disconnected (Socket: %d)\n", client_socket);
                break;
            }

            printf("%s: %s\n", client_name, buffer);
            char broadcast_message_buffer[BUFFER_SIZE + 50];
            snprintf(broadcast_message_buffer, sizeof(broadcast_message_buffer), "%s: %s", client_name, buffer);
            broadcast_message(broadcast_message_buffer, client_socket);
        }
    }

    EnterCriticalSection(&client_list_lock);
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].socket == client_socket) {
            clients[i] = clients[client_count - 1];
            client_count--;
            break;
        }
    }
    LeaveCriticalSection(&client_list_lock);

    printf("Closing socket: %d\n", client_socket);
    closesocket(client_socket);
    return 0;
}

int main() {
    WSADATA wsaData;
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    HANDLE thread_id;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        perror("WSAStartup failed");
        exit(EXIT_FAILURE);
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        perror("Socket creation failed");
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) == SOCKET_ERROR) {
        perror("Setsockopt failed");
        closesocket(server_fd);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(atoi(PORT));

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        perror("Bind failed");
        closesocket(server_fd);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) == SOCKET_ERROR) {
        perror("Listen failed");
        closesocket(server_fd);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %s\n", PORT);

    InitializeCriticalSection(&client_list_lock);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket == INVALID_SOCKET) {
            perror("Accept failed");
            continue;
        }

        printf("Connection accepted (Socket: %d)\n", new_socket);

        SOCKET *client_socket_ptr = (SOCKET*)malloc(sizeof(SOCKET));
        if (!client_socket_ptr) {
            perror("Memory allocation failed");
            closesocket(new_socket);
            continue;
        }

        *client_socket_ptr = new_socket;

        thread_id = CreateThread(NULL, 0, handle_client, (void*)client_socket_ptr, 0, NULL);
        if (thread_id == NULL) {
            printf("Thread creation failed with error: %d\n", GetLastError());
            closesocket(new_socket);
            free(client_socket_ptr);
        } else {
            CloseHandle(thread_id);
        }
    }

    closesocket(server_fd);
    WSACleanup();
    DeleteCriticalSection(&client_list_lock);

    return 0;
}
