#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 32454
#define SERVER_IP "127.0.0.1"

int ind = -1;

void sendData(int sockfd, const char* data, size_t length, struct sockaddr_in* server_addr) {
    if (data[0] == '6') {
        printf("Отправил запрос на подключение к серверу.\n");
    }
    
    if (sendto(sockfd, data, length, 0, (struct sockaddr*)server_addr, sizeof(*server_addr)) == -1) {
        perror("Ошибка при отправке данных");
        exit(EXIT_FAILURE);
    }
}

int parseResponse(char* buffer) {
      if (buffer[0] == '6') {
          return 0; // пришел ответ на авторизацию.
      } else if (buffer[0] == '7') {
          int total;
          if (sscanf(buffer, "%*c %*d %d", &total) != 1) {
              // Ошибка при извлечении второго числа
              printf("Ошибка при извлечении ответа сервера\n");
              exit(-1);
          }
          
          return total;
      } 
      
      printf("Неизвестный ответ от сервера %s\n", buffer);
      exit(-1);
}

int receiveData(int sockfd, char* buffer, size_t length, struct sockaddr_in* addr) {
    fflush(stdout);

    socklen_t addr_size = sizeof(addr);
    int received = recvfrom(sockfd, buffer, length, 0, (struct sockaddr*)addr, &addr_size);
    if (received <= 0) {
        if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Непредвиденная ошибка при вызове recv");
        }
        
        return -1;
    }

    int receivedInd;
    if (sscanf(buffer, "%*c %d", &receivedInd) != 1) {
        printf("Ошибка при извлечении индекса\n");
        exit(-1);
    }

    if (receivedInd == -1) {
        printf("Сервер отклонил подключение(все игровые комнаты заняты)!\n");
        exit(0);
    }

    if (ind != receivedInd) {
        return -1;
    }

    return parseResponse(buffer);
}


char Sender(int sockfd, struct sockaddr_in* addr, char* buffer, size_t length) {
    int tries = 0, totalTries = 40;

    while (tries <= 40) {
        sendData(sockfd, buffer, length, addr);
        int total = receiveData(sockfd, buffer, length, addr);

        // Ошибка при отправке пакета.
        if (total == -1) {
            ++tries;
            continue;
        }

        return total;
    }

    printf("Соединение было разорвано!(превышено время ожидания ответа от сервера 10s).");
    exit(0);
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    ind = rand() % 1000000;

    printf("Наблюдатель успешно запущен!\n");
    
    int clientSocket;
    struct sockaddr_in addr;

    // Создание сокета
    if ((clientSocket = socket(AF_INET, SOCK_DGRAM, 0)) <= 0) {
        perror("Ошибка создания сокета");
        exit(EXIT_FAILURE);
    }

    // Установка блокирующего режима
    if (fcntl(clientSocket, F_SETFL, 0) < 0) {
        perror("fcntl");
        return 1;
    }

    memset(&addr, '\0', sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    struct timeval timeout;
    timeout.tv_sec = 0;  // Установка времени ожидания в 250 ms
    timeout.tv_usec = 250000;

    if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        perror("Ошибка при установке времени ожидания");
        exit(EXIT_FAILURE);
    }

    int bufferSize = 64;
    char* buf = (char*)malloc(bufferSize * sizeof(char));

    memset(buf, 0, 64);
    // Создание запроса на подключение к серверу.
    int result = snprintf(buf, bufferSize, "6 %d", ind);
    if (result < 0 || result >= bufferSize) {
        close(clientSocket);
        printf("Ошибка при форматировании строки!\n");
        exit(EXIT_FAILURE);
    }

    Sender(clientSocket, &addr, buf, 64);
    printf("Наблюдатель с ind: %d успешно подключился к серверу!\n", ind);

    int prev = -1;
    while (1) {
        memset(buf, 0, 64);
        snprintf(buf, 64, "7 %d", ind);
        int totalClients = Sender(clientSocket, &addr, buf, 64);
        if (prev != totalClients / 2) {
            printf("На сервере %d игровых сессий.\n", totalClients / 2);
        }
        
        prev = totalClients / 2;
    }

    close(clientSocket);
    return 0;
}
