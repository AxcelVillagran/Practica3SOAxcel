#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>
#include <curl/curl.h>

//Puerto para la conexion
#define PORT 8080
//Maximo de clientes definido
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
//CPU THRESHOLD para el envio de la alerta en caso de superar el valor en cpu_usage
#define CPU_THRESHOLD 85  
//LOAD_THESHOLD para el envio de la alerta en caso de superar el valor en load_avg1
#define LOAD_THRESHOLD 2.0

const char *TWILIO_ACCOUNT_SID = "AC2b47f8ca808d3701725483c2a2e4086e";
const char *TWILIO_AUTH_TOKEN = "9dfcb90d009fa4735b7e3e8dce30f5da";
const char *TWILIO_PHONE_NUMBER = "+17755876995"; 
const char *TO_PHONE_NUMBER = "+593987151215";    // MI numero de telefono

//Mutex usado para 
pthread_mutex_t update_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char client_name[BUFFER_SIZE];
    int cpu_usage;
    int memory_usage;
    int disk_usage;
    long uptime;
    double load_avg1;
    double load_avg5;
    int active; //Variable para indicar si el cliente esta activo
} client_data_t;

//Arreglo de clientes
client_data_t clients[MAX_CLIENTS];
int client_count = 0;
int update_interval;

//Funcion que utiliza la API de Twilio para enviar alertas
void send_twilio_alert(const char *metric, const char *client_name, int value) {
    CURL *curl;
    CURLcode res;
    char postfields[1024];

    snprintf(postfields, sizeof(postfields),
             "To=%s&From=%s&Body=ALERTA: %s del cliente %s ha excedido el límite. Valor: %d%%",
             TO_PHONE_NUMBER, TWILIO_PHONE_NUMBER, metric, client_name, value);

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.twilio.com/2010-04-01/Accounts/AC2b47f8ca808d3701725483c2a2e4086e/Messages.json");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);
        curl_easy_setopt(curl, CURLOPT_USERNAME, TWILIO_ACCOUNT_SID);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, TWILIO_AUTH_TOKEN);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "Error enviando alerta: %s\\n", curl_easy_strerror(res));
        } else {
            printf("Alerta enviada correctamente a %s\\n", TO_PHONE_NUMBER);
        }

        curl_easy_cleanup(curl);
    }
}

//Funcion para actualizar el dashboard
void *dashboard_updater(void *arg) {
    while (1) {
        pthread_mutex_lock(&update_mutex);

        printf("\033[H\033[J");
        printf("\n===== DashProm - Real-Time Dashboard =====\n");
        printf("Client Name        | CPU Usage | Mem Usage | Disk Usage | Uptime (s) | Load1  | Load5\n"); //Encabezado del dashboard
        printf("-------------------|-----------|-----------|------------|------------|--------|--------\n");

        for (int i = 0; i < client_count; i++) {
            if (clients[i].active) { // Mostrar solo los clientes activos y no pasados
                printf("%-18s| %9d%% | %9d%% | %10d%% | %10ld | %6.2f | %6.2f\n",
                       clients[i].client_name,
                       clients[i].cpu_usage,
                       clients[i].memory_usage,
                       clients[i].disk_usage,
                       clients[i].uptime,
                       clients[i].load_avg1,
                       clients[i].load_avg5);
            }
        }

        pthread_mutex_unlock(&update_mutex);

        sleep(1); 
    }
    return NULL;
}

//Funcion para manejar las metricas enviadas a traves del socket por el cliente
void *receive_metrics(void *sent_socket) {
    //Creacion del socket
    int metrics_socket = *(int *)sent_socket;
    free(sent_socket);
    char buffer[BUFFER_SIZE];
    // Se envia el update_interval para que el cliente sepa cuando enviar nuevas metricas
    if (send(metrics_socket, &update_interval, sizeof(update_interval), 0) <= 0) {
        perror("No se pudo enviar el update_interval");
        close(metrics_socket);
        pthread_exit(NULL);
    }

    while (1) {
        int received_bytes = recv(metrics_socket, buffer, sizeof(buffer) - 1, 0);
        if (received_bytes <= 0) {
            close(metrics_socket);
            pthread_exit(NULL);
        }
        buffer[received_bytes] = '\0';

        //Variables para almacenar las metricas obtenidas
        char client_name[BUFFER_SIZE];
        int cpu_usage, memory_usage, disk_usage;
        long uptime;
        double load_avg1, load_avg5;

        // Parseo de metricas del cliente y el nombre del sistema
        if (sscanf(buffer, "Client %[^:]: CPU:%d%%, Memory:%d%%, Disk:%d%%, Uptime:%ld, Load1:%lf, Load5:%lf",
                   client_name, &cpu_usage, &memory_usage, &disk_usage, &uptime, &load_avg1, &load_avg5) < 7) {
            fprintf(stderr, "Metricas incompletas: %s\n", buffer);
            continue;
        }

        pthread_mutex_lock(&update_mutex);
        //Validacion de thresholds para enviar alertas
        if (cpu_usage > CPU_THRESHOLD) {
                send_twilio_alert("CPU", client_name, cpu_usage);
        }
        if (load_avg1 > LOAD_THRESHOLD) {
            send_twilio_alert("Load Average (1 min)", client_name, load_avg1);
        }

        // Actualizacion de las metricas
        int found = 0;
        for (int i = 0; i < client_count; i++) {
            if (strcmp(clients[i].client_name, client_name) == 0) {
                clients[i].cpu_usage = cpu_usage;
                clients[i].memory_usage = memory_usage;
                clients[i].disk_usage = disk_usage;
                clients[i].uptime = uptime;
                clients[i].load_avg1 = load_avg1;
                clients[i].load_avg5 = load_avg5;
                clients[i].active = 1; 
                found = 1;
                break;
            }
        }

        if (!found && client_count < MAX_CLIENTS) {
            strncpy(clients[client_count].client_name, client_name, BUFFER_SIZE);
            clients[client_count].cpu_usage = cpu_usage;
            clients[client_count].memory_usage = memory_usage;
            clients[client_count].disk_usage = disk_usage;
            clients[client_count].uptime = uptime;
            clients[client_count].load_avg1 = load_avg1;
            clients[client_count].load_avg5 = load_avg5;
            clients[client_count].active = 1; 
            client_count++;
        }

        pthread_mutex_unlock(&update_mutex);
    }

    close(metrics_socket);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <update_interval_in_seconds>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    update_interval = atoi(argv[1]);
    if (update_interval <= 0) {
        fprintf(stderr, "Error: Update interval must be a positive integer.\n");
        exit(EXIT_FAILURE);
    }

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket fallido");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind fallido");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen fallido");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor escuchando en puerto %d con update interval %d segundos\n", PORT, update_interval);

    // Start the dashboard updater thread
    pthread_t dashboard_thread;
    pthread_create(&dashboard_thread, NULL, dashboard_updater, NULL);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            perror("Accept fallido");
            continue;
        }

        pthread_t thread_id;
        int *client_socket = malloc(sizeof(int));
        *client_socket = new_socket;
        pthread_create(&thread_id, NULL, receive_metrics, client_socket);
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}
