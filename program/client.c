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

char* field;
char* opponentField;

void printField(int N) {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            printf("%c", field[i * N + j]);
        }
        printf("\n");
    }
}

void printOpponentField(int N) {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            printf("%c", opponentField[i * N + j]);
        }
        printf("\n");
    }
}

void fillOponentField(int N, int mortarsCount) {
    opponentField = (char*)malloc(N * N * sizeof(char));
    for (int i = 0; i < N * N; ++i) {
        opponentField[i] = '?';
    }

    // Заполняем поле.
    for (int i = 0; i < mortarsCount; ++i) {
        while (1) {
            int i = rand() % N;
            int j = rand() % N;

            // В этой ячейке уже есть орудие.
            if (opponentField[i * N + j] == '1') {
                continue;
            }

            opponentField[i * N + j] = '1';
            break;
        }
    }
}

void fillField(int N, int mortarsCount) {
    field = (char*)malloc(N * N * sizeof(char));
    for (int i = 0; i < N * N; ++i) {
        field[i] = '0';
    }

    // Заполняем поле.
    for (int i = 0; i < mortarsCount; ++i) {
        while (1) {
            int i = rand() % N;
            int j = rand() % N;

            // В этой ячейке уже есть орудие.
            if (field[i * N + j] == '1') {
                continue;
            }

            field[i * N + j] = '1';
            break;
        }
    }
}

int shoot(int N) {
    while (1) {
        int i = rand() % N;
        int j = rand() % N;

        // Уже стреляли и уничтожили.
        if (opponentField[i * N + j] == 'X') {
            continue;
        }

        // Уже стреляли и промахнулись.
        if (opponentField[i * N + j] == '.') {
            continue;
        }

        return (i * N + j);
    }
}

bool checkLose(int N) {
    for (int i = 0; i < N * N; ++i) {
        if (field[i] == '1') {
            return false;
        }
    }

    return true;
}


struct Request
{
    int opType; // Тип операции(0 - авторизация, 1 - выстрел по игроку, 2 - прием результата выстрела, 3 - выстрел по нам, 4 - отправка рез выстрела по нам)
    int coords; // Координаты выстрела.
    char gameStatus; // 'W' - игра не началась(ожидание подключения), 'S' - игра начинается, 'F' - игра завершилась победой, 'D' - соперник покинул игру
    // '.' - промах, 'X' - орудие уничтожено
    int firstMove; // 0 - нет, 1 - да.
};

void sendData(int sockfd, const char* data, size_t length, struct sockaddr_in* server_addr) {
    printf("Отправил запрос на сервер: %c (%s)\n", data[0], data);
    if (sendto(sockfd, data, length, 0, (struct sockaddr*)server_addr, sizeof(*server_addr)) == -1) {
        perror("Ошибка при отправке данных");
        exit(EXIT_FAILURE);
    }
}

struct Request parseResponse(char* buffer) {
      if (buffer[0] == '0') {
          struct Request request = {0, -1, -1, -1};
          return request;
      } else if (buffer[0] == '1') {
          char receivedOpType;
          int receivedFirstMove;
          if (sscanf(buffer, "%*c %*d %c %d", &receivedOpType, &receivedFirstMove) != 2) {
              // Ошибка при извлечении второго числа
              printf("Ошибка при извлечении ответа сервера\n");
              exit(-1);
          }

          struct Request request = {1, -1, receivedOpType, receivedFirstMove};
          return request;
      } else if (buffer[0] == '2') {
          struct Request request = {2, -1, -1, -1};
          return request;
      } else if (buffer[0] == '3') {
          char receivedFireResult;
          if (sscanf(buffer, "%*c %*d %c", &receivedFireResult) != 1) {
              printf("Ошибка при извлечении координат\n");
              exit(-1);
          }

          struct Request request = {3, -1, receivedFireResult, -1};
          return request;
      } else if (buffer[0] == '4') {
          int coords;
          if (sscanf(buffer, "%*c %*d %d", &coords) != 1) {
              printf("Ошибка при извлечении координат\n");
              exit(-1);
          }

          struct Request request = {4, coords, -1, -1};
          return request;
      } else if (buffer[0] == '5') {
          char receivedFireResult;
          if (sscanf(buffer, "%*c %*d %c", &receivedFireResult) != 1) {
              printf("Ошибка при извлечении данных от сервера\n");
              exit(-1);
          }

          struct Request request = {5, -1, receivedFireResult, -1};
          return request;
      }

      printf("Неизвестный ответ от сервера %s\n", buffer);
      exit(-1);
}

struct Request receiveData(int sockfd, char* buffer, size_t length, struct sockaddr_in* addr) {
    while (1) {
        fflush(stdout);

        socklen_t addr_size = sizeof(addr);
        int received = recvfrom(sockfd, buffer, length, 0, (struct sockaddr*)addr, &addr_size);
        if (received <= 0) {
            if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Непредвиденная ошибка при вызове recv");
            }

            struct Request request = {-1, -1, -1, -1};
            return request;
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
            struct Request request = {-1, -1, -1, -1};
            return request;
        }

        struct Request request = parseResponse(buffer);
        return request;
      }
}


struct Request Sender(int sockfd, struct sockaddr_in* addr, char* buffer, size_t length) {
    int tries = 0, totalTries = 40;

    while (tries <= 40) {
        sendData(sockfd, buffer, length, addr);
        struct Request request = receiveData(sockfd, buffer, length, addr);

        // Ошибка при отправке пакета.
        if (request.opType == -1) {
            ++tries;
            continue;
        }

        return request;
    }

    printf("Соединение было разорвано!(превышено время ожидания ответа от сервера 10s).");
    exit(0);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("you need to enter 2 arguments: 1) size of the battlefield; 2) count of the mortars.");
        exit(0);
    }

    srand(time(NULL));
    ind = rand() % 1000000;

    printf("Клиент успешно запущен!\n");

    int N = atoi(argv[1]); // Размер поля боя.

    int mortarsCount = atoi(argv[2]); // Количество минометов.
    srand(time(NULL)); // для рандомной генерации.

    fillField(N, mortarsCount);
    fillOponentField(N, mortarsCount);

    printf("Поле успешно сгенерировано!\n");
    printField(N);

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
    int result = snprintf(buf, bufferSize, "0 %d %d %d", ind, N, mortarsCount);
    if (result < 0 || result >= bufferSize) {
        close(clientSocket);
        printf("Ошибка при форматировании строкового переданных вами чисел (0 N и MortarsCount)!\n");
        exit(EXIT_FAILURE);
    }

    Sender(clientSocket, &addr, buf, 64);
    printf("Клиент успешно подключился к серверу, (id: %d) ждем начала игры!\n", ind);

    struct Request ans;
    while (true) {
        memset(buf, 0, 64);
        sprintf(buf, "1 %d", ind);

        ans = Sender(clientSocket, &addr, buf, bufferSize);
        if (ans.gameStatus == 'W') {
            printf("Лобии пока не заполнено, ждем 2 секунды до следующего запроса о начале игры.\n");
            sleep(2);
            continue;
        } else if (ans.gameStatus == 'S') {
            break;
        }
    }

    printf("WAKU");
    bool isAttack = false;
    if (ans.firstMove == 1) {
        isAttack = true;
    }

    if (isAttack) {
        printf("Другой игрок подключился, игра началась, хожу первым.\n");
    } else {
        printf("Другой игрок подключился, игра началась, хожу вторым.\n");
    }

    while (1) {
        if (isAttack) {
            int coordsToShoot = shoot(N);
            printf("Стреляю по координатам:  (%d, %d) [%d].\n", coordsToShoot / N, coordsToShoot - (coordsToShoot / N) * N, coordsToShoot);

            memset(buf, 0, 64);
            sprintf(buf, "2 %d %d", ind, coordsToShoot);
            Sender(clientSocket, &addr, buf, 64);

            memset(buf, 0, 64);
            sprintf(buf, "3 %d", ind);
            struct Request result = Sender(clientSocket, &addr, buf, 64);

            if (result.gameStatus == 'D') {
                printf("Соперник покинул игру, ПОБЕДА!!!");
                break;
            }

            if (result.gameStatus == 'F') {
                printf("Соперник был уничтожен выстрелом, ПОБЕДА!!!");
                break;
            }

            if (result.gameStatus == '.') {
                printf("Промах!\n");
            } else if (result.gameStatus == 'X') {
                printf("Уничтожил орудие!\n");
            }

            opponentField[coordsToShoot] = result.gameStatus; // Сохраняем данные о выстреле.
            isAttack = false;
        } else {
            memset(buf, 0, 64);
            sprintf(buf, "4 %d", ind);
            struct Request result = Sender(clientSocket, &addr, buf, 64);

            if (result.gameStatus == 'D') {
                printf("Соперник покинул игру, ПОБЕДА!!!");
                break;
            }

            int coords = result.coords;
            memset(buf, 0, 64);
            if (field[coords] == '0') {
                sprintf(buf, "5 %d .", ind);
            } else if (field[coords] == '1') {
                field[coords] = '2';
                sprintf(buf, "5 %d X", ind);
            } else {
                printf("Ошибка при формировании результата выстрела %d (%d)", coords, field[coords]);
                perror("Ошибка при формировании результата выстрела");
                exit(EXIT_FAILURE);
            }

            if (checkLose(N)) {
                printf("Противник победил, уничтожив последнюю клетку: (%d, %d).\n", coords / N, coords - (coords / N) * N);

                memset(buf, 0, 64);
                sprintf(buf, "5 %d F", ind);
                Sender(clientSocket, &addr, buf, 64);
                break;
            }

            printf("Противник выстрелил по клетке: (%d, %d).\n", coords / N, coords - (coords / N) * N);
            Sender(clientSocket, &addr, buf, 64);

            int lives = 0;
            for (int i = 0; i < N * N; ++i) {
                if (field[i] == '1') {
                    lives++;
                }
            }
            printf("Жизней: %d.\n", lives);

            isAttack = true;
        }
    }

    close(clientSocket);
    return 0;
}
