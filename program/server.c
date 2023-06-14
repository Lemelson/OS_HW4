#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <poll.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 32454
#define MAX_CLIENTS 100
#define MAX_VISITORS 100

struct ClientInfo {
    int n; // Размер поля.
    int mortarsCount; // Количество пушек.
    int ind; // Индекс сервера.

    int shootCoord;  // Координаты последнего выстрела
    char shootResult; // Результаты последнего выстрела
};

struct Shot {
  int turn; // id того, чей сейчас ход
  time_t lastRequest;
};

struct ListenParams {
    int sockfd;
    struct sockaddr_in serverAddr;
};

struct ObserverInfo {
    int ind;
    time_t lastRequest;
};

struct ClientInfo clients[MAX_CLIENTS]; // Массив клиентов.
struct ObserverInfo observers[MAX_VISITORS]; // Массив наблюдателей.
struct Shot roomsInfo[MAX_CLIENTS / 2];
int totalClients;

int updateConnections() {
    time_t curTime;
    for (int i = 0; i < MAX_CLIENTS; i += 2) {
        if (clients[i].ind == -1 || clients[i + 1].ind == -1) {
            continue;
        }
        
        time(&curTime);
        double diff = difftime(curTime, roomsInfo[i / 2].lastRequest);
        if (diff >= 10.0) {
            printf("Игра в лобби: %d завершилась.\n", i / 2);
            clients[i].n = -1;
            clients[i].mortarsCount = -1;
            clients[i].ind = -1;
            
            clients[i + 1].n = -1;
            clients[i + 1].mortarsCount = -1;
            clients[i + 1].ind = -1;
            
            totalClients -=2;
        }
    }
    
    for (int i = 0; i < MAX_VISITORS; ++i) {
        if (observers[i].ind == -1) {
            continue;
        }
        
        time(&curTime);
        double diff = difftime(curTime, observers[i].lastRequest);
        if (diff >= 10.0) {
            printf("Наблюдатель с (ind, id): (%d, %d) отключился:\n", observers[i].ind, i);
            observers[i].ind = -1;
            observers[i].lastRequest = -1;
        }
    }
}

int addNewClient(int n, int mortarsCount, int ind) {
    for (int i = 1; i < MAX_CLIENTS; i += 2) {
        if (clients[i].ind != -1) {
            continue;
        }

        if (clients[i - 1].n == -1) {
            continue;
        }

        if (clients[i - 1].n == n && clients[i - 1].mortarsCount == mortarsCount) {
            clients[i].n = n;
            clients[i].mortarsCount = mortarsCount;
            clients[i].ind = ind;
            ++totalClients;
            printf("Добавили клиента (ind, id) : (%d %d)\n", ind, i);
            return i;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i += 2) {
        if (clients[i].n == -1) {
            clients[i].n = n;
            clients[i].mortarsCount = mortarsCount;
            clients[i].ind = ind;
            ++totalClients;
            printf("Добавили клиента (ind, id) : (%d %d)\n", ind, i);
            return i;
        }
    }

    return -1;
}

int isRoomFullById(int ind) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].ind == ind) {
            if (i % 2 == 1) {
                return i;
            }

            if (clients[i + 1].ind != -1) {
                return i;
            }

            return -2;
        }
    }

    return -1;
}

int getEnemyPos(int pos) {
  return (pos % 2 == 0 ? (pos + 1) : (pos - 1));
}

int findByInd(int ind){
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].ind == ind) {
            return i;
        }
    }

    return -1;
}

void sendData(int sockfd, const char* data, size_t length, struct sockaddr_in* client_addr) {
    if (data[0] != '7') {
        printf("Отправил ответ на клиенту: %c (%s)\n", data[0], data);
        fflush(stdout);
    }

    if (sendto(sockfd, data, length, 0, (struct sockaddr*)client_addr, sizeof(*client_addr)) == -1) {
        perror("Ошибка при отправке данных");
        exit(EXIT_FAILURE);
    }
}

int findObserverPos(int ind) {
    for (int i = 0; i < MAX_VISITORS; ++i) {
        if (observers[i].ind == ind) {
            return i;
        }
    }
    
    return -1;    
}

int findNewObserverPos(int ind) {
    for (int i = 0; i < MAX_VISITORS; ++i) {
        if (observers[i].ind == -1) {
            observers[i].ind = ind;
            printf("Наблюдатель с id: %d был добавлен в массив наблюдателей на позицию: %d\n", ind, i);
            return i;
        }
    }
    
    return -1;
}

void processEvent(char* buf, int sockfd, struct sockaddr_in* client_addr) {
    updateConnections();
    int ind;
    if (sscanf(buf, "%*c %d", &ind) != 1) {
        // Ошибка при извлечении второго числа
        printf("Ошибка при извлечении ind клиента (%s).\n", buf);
        exit(-1);
    }

    if (buf[0] >= '0' && buf[0] <= '5') {
        if (findByInd(ind) != -1) {
            int curPos = findByInd(ind);
            time(&roomsInfo[curPos].lastRequest);
        }
    }

    if (buf[0] >= '6' && buf[0] <= '7') {
        if (findObserverPos(ind) != -1) {
            int curPos = findObserverPos(ind);
            time(&observers[curPos].lastRequest);
        }
    }

    if (buf[0] == '0') {
        int receivedN;
        int receivedMortarsCount;
        if (sscanf(buf, "%*c %*d %d %d", &receivedN, &receivedMortarsCount) != 2) {
            printf("%d %d", receivedN, receivedMortarsCount);
            printf("Ошибка при извлечении координат\n");
            exit(-1);
        }

        int newInd = addNewClient(receivedN, receivedMortarsCount, ind);
        memset(buf, 0, 64);
        if (newInd != -1) {
            snprintf(buf, 64, "0 %d", ind);
        } else {
            snprintf(buf, 64, "0 %d", newInd);
        }
        sendData(sockfd, buf, 64, client_addr);
    } else if (buf[0] == '1') {
        memset(buf, 0, 64);
        int pos = isRoomFullById(ind);
        if (pos == -1) {
            printf("Клиент передал некорректный id/n");
            return;
        } else if (pos == -2) {
            printf("Отказали клиенту с id %d в начале игры, ждем пока лобби: %d заполнится!\n", ind, ind / 2);
            int num = -1;
            snprintf(buf, 64, "1 %d W %d", ind, num);
            sendData(sockfd, buf, 64, client_addr);
            return;
        }

        int roomPos = (pos / 2);
        int enemyPos = getEnemyPos(pos);

        bool myMove = (roomsInfo[roomPos].turn == ind);
        if (roomsInfo[roomPos].turn == -1) {
          srand(time(NULL));

          myMove = rand() % 2;
          if (myMove) {
            roomsInfo[roomPos].turn = clients[pos].ind;
          } else {
            roomsInfo[roomPos].turn = clients[enemyPos].ind;
          }
        }
        clients[pos].shootCoord = -1;
        clients[pos].shootResult = 'E';

        memset(buf, 0, 64);
        snprintf(buf, 64, "1 %d S %d", ind, myMove);
        sendData(sockfd, buf, 64, client_addr);
    } else if (buf[0] == '2') {
        int receivedCords;
        if (sscanf(buf, "%*c %*d %d", &receivedCords) != 1) {
            // Ошибка при извлечении второго числа
            printf("Ошибка при извлечении координат\n");
            exit(-1);
        }

        int pos = findByInd(ind);
        if (pos == -1) {
            perror("Ошибка, пользователь с (ind: %d) не найден\n");
            return;
        }

        int roomPos = (pos / 2);

        if (roomsInfo[roomPos].turn != ind && clients[pos].shootCoord != receivedCords) {
            perror("Ошибка: сейчас ход оппонента\n");
            return;
        }

        if (clients[pos].shootCoord != receivedCords) {
          clients[pos].shootResult = 'E';
          clients[pos].shootCoord = receivedCords;
        }

        memset(buf, 0, 64);
        snprintf(buf, 64, "2 %d", ind);
        sendData(sockfd, buf, 64, client_addr);
    } else if (buf[0] == '3') {
        int pos = findByInd(ind);
        if (pos == -1) {
            perror("Ошибка, пользователь с (ind: %d) не найден\n");
            return;
        }
        int roomPos = (pos / 2);

        if (clients[pos].shootResult == 'E') {
          printf("Ждём ответ оппонента о результате нашего хода (%d)\n", ind);
          return;
        }

        memset(buf, 0, 64);
        snprintf(buf, 64, "3 %d %c", ind, clients[pos].shootResult);
        sendData(sockfd, buf, 64, client_addr);
    } else if (buf[0] == '4') {
        int pos = findByInd(ind);
        if (pos == -1) {
            perror("Ошибка, пользователь с (ind: %d) не найден\n");
            return;
        }
        int roomPos = (pos / 2);
        int enemyPos = getEnemyPos(pos);

        if (clients[enemyPos].shootCoord == -1) {
          printf("Ждём ответ о координатах хода оппонента (%d)\n", ind);
          return;
        }

        memset(buf, 0, 64);
        snprintf(buf, 64, "4 %d %d", ind, clients[enemyPos].shootCoord);
        sendData(sockfd, buf, 64, client_addr);
    } else if (buf[0] == '5') {
        int pos = findByInd(ind);
        if (pos == -1) {
            perror("Ошибка, пользователь с (ind: %d) не найден\n");
            return;
        }
        int roomPos = (pos / 2);
        int enemyPos = getEnemyPos(pos);

        char receivedType;
        if (sscanf(buf, "%*c %*d %c", &receivedType) != 1) {
            printf("Ошибка при извлечении координат\n");
            exit(-1);
        }

        roomsInfo[roomPos].turn = clients[pos].ind;

        clients[enemyPos].shootResult = receivedType;

        memset(buf, 0, 64);
        snprintf(buf, 64, "5 %d %c", ind, clients[enemyPos].shootResult);
        sendData(sockfd, buf, 64, client_addr);
    } else if (buf[0] == '6') { // Авторизация observerа.
        int pos = findNewObserverPos(ind);
        if (pos == -1) {
            printf("Отклонили подключение наблюдателя, на сервере слишком много наблюдателей!/n");
            return;
        }
        snprintf(buf, 64, "6 %d", ind);
        sendData(sockfd, buf, 64, client_addr);
    } else if (buf[0] == '7') {
        memset(buf, 0, 64);
        printf("%s", buf);
        snprintf(buf, 64, "7 %d %d", ind, totalClients);
        sendData(sockfd, buf, 64, client_addr);
    }
    
    updateConnections();
}

void listenPort(struct ListenParams* params) {
    int sockfd = params->sockfd;
    struct sockaddr_in serverAddr = params->serverAddr;

    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    char* buffer = (char*)malloc(64 * sizeof(char));

    printf("Поток для прослушивания порта запущен\n");

    while (1) {
        // Очистка буфера
        memset(buffer, 0, 64);

        // Получение данных от клиента
        socklen_t addr_size = sizeof(client_addr);
        ssize_t recv_len = recvfrom(sockfd, buffer, 64, 0, (struct sockaddr*)&client_addr, &addr_size);
        if (recv_len == -1) {
            perror("Ошибка при получении данных от клиента.");
            exit(EXIT_FAILURE);
        }
        // printf("buffer (%s), recv_len (%ld)\n", buffer, recv_len);
        processEvent(buffer, sockfd, &client_addr);
    }
}

// В программе используются операции 3 типов: 0 - подключение игрока, 1 - подключение наблюдателя
//                                            2 - выстрел стороной клиентом, 3 - данные о том, произошло ли попадание по стороне-клиенту
int main()
{
    totalClients = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        clients[i].n = -1;
        clients[i].mortarsCount = -1;
        clients[i].ind = -1;
    }

    for (int i = 0; i < MAX_CLIENTS / 2; ++i) {
        roomsInfo[i].turn = -1;
        time(&roomsInfo[i].lastRequest);
    }

    for (int i = 0; i < MAX_VISITORS; ++i) {
        observers[i].ind = -1;
        time(&observers[i].lastRequest);
    }

    int serverSocket;
    struct sockaddr_in serverAddr;

    // Создание сокета
    if ((serverSocket = socket(AF_INET, SOCK_DGRAM, 0)) == 0)
    {
        perror("Ошибка создания сокета");
        exit(EXIT_FAILURE);
    }

    // Установка блокирующего режима
    if (fcntl(serverSocket, F_SETFL, 0) < 0) {
        perror("fcntl");
        return 1;
    }

    int option_value = 1;
    // Бинд сервера на отправку широковещательных сообщений(чтобы отправка производилась всем клиентам локальной сети).
    setsockopt(serverSocket, SOL_SOCKET, SO_BROADCAST, &option_value, sizeof(option_value));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    // Привязка сокета к указанному порту
    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("Ошибка привязки сокета");
        exit(EXIT_FAILURE);
    }

    printf("Сервер успешно запущен и ожидает подключений...\n");

    struct ListenParams params;
    params.sockfd = serverSocket;  // Пример значения для sockfd
    params.serverAddr = serverAddr;
    listenPort(&params);

    return 0;
}
