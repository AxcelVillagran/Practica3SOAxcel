#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>

//IP del servidor
#define SERVER_IP "192.168.3.2"
//Puerto en el que se conecta al servidor
#define SERVER_PORT 8080
#define BUFFER_SIZE 1024

//Mutex usado para el manejo de las metricas
pthread_mutex_t metrics_mutex = PTHREAD_MUTEX_INITIALIZER;

// Funcion para obtener la ip del cliente
void get_client_ip(char *ip_buffer) {
    FILE *fp = popen("hostname -I | awk '{print $1}'", "r");
    if (fp == NULL) {
        perror("Error al obtener la direccion IP");
        exit(EXIT_FAILURE);
    }
    fgets(ip_buffer, BUFFER_SIZE, fp);
    strtok(ip_buffer, "\n"); 
    pclose(fp);
}

// Function para calcular el uso de CPU
int calculate_cpu_usage() {
    static long prev_idle = 0, prev_total = 0;
    long idle, total;
    long diff_idle, diff_total;

    FILE *fp = fopen("/proc/stat", "r");
    if (fp == NULL) {
        perror("Error opening /proc/stat");
        return -1;
    }

    char buffer[BUFFER_SIZE];
    fgets(buffer, sizeof(buffer), fp); //Lectura de las estadisticas de la CPU
    fclose(fp);

    long user, nice, system, idle_time, iowait, irq, softirq, steal;
    sscanf(buffer, "cpu  %ld %ld %ld %ld %ld %ld %ld %ld", &user, &nice, &system, &idle_time, &iowait, &irq, &softirq, &steal);

    idle = idle_time + iowait;
    total = user + nice + system + idle + irq + softirq + steal;

    diff_idle = idle - prev_idle;
    diff_total = total - prev_total;

    prev_idle = idle;
    prev_total = total;

    if (diff_total == 0) return 0; 

    return (int)((100 * (diff_total - diff_idle)) / diff_total);
}

void get_client_metrics(char *metrics_buffer, const char *client_name) {
    struct sysinfo sys_info;
    struct statvfs fs_info;
    if (sysinfo(&sys_info) != 0) {
        perror("sysinfo error");
        exit(EXIT_FAILURE);
    }
    //Obtener el uso de CPU
    int cpu_usage_final = calculate_cpu_usage();

    // Uso de sys_info para obtener los datos de memoria
    int total_memory = sys_info.totalram / 1024 / 1024;
    int free_memory = sys_info.freeram / 1024 / 1024;
    int used_memory = total_memory - free_memory;
    int memory_usage_final = (used_memory * 100) / total_memory;
    if (statvfs("/", &fs_info) != 0) {
        perror("statvfs error");
        exit(EXIT_FAILURE);
    }
    //Uso de fs_info para obtener la informacion del disco
    long total_disk = (fs_info.f_blocks * fs_info.f_frsize) / 1024 / 1024;
    long free_disk = (fs_info.f_bfree * fs_info.f_frsize) / 1024 / 1024;
    long used_disk = total_disk - free_disk;
    int disk_usage_final = (used_disk * 100) / total_disk;
    //Uso de sys_info para obtener uptime, loadavg1 y loadavg2
    long uptime = sys_info.uptime;
    double load_avg1 = sys_info.loads[0] / 65536.0; //loadavg1 es la carga del sistema en el ultimo minuto
    double load_avg5 = sys_info.loads[1] / 65536.0; //loadavg5 es la carga del sistema en los ultimos 5 minutos

    snprintf(metrics_buffer, BUFFER_SIZE,
             "Client %s: CPU:%d%%, Memory:%d%%, Disk:%d%%, Uptime:%ld, Load1:%.2f, Load5:%.2f",
             client_name, cpu_usage_final, memory_usage_final, disk_usage_final, uptime, load_avg1, load_avg5);
}

//Funcion para enviar las metricas al servidor
void send_metrics(const char *server_ip, int server_port, const char *client_name) {
    int metrics_socket;
    struct sockaddr_in server_address;
    char metrics_buffer[BUFFER_SIZE];
    int update_interval;

    if ((metrics_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error al crear el socket");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) {
        perror("Direccion no disponible");
        close(metrics_socket);
        exit(EXIT_FAILURE);
    }

    if (connect(metrics_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Coneccion fallida");
        close(metrics_socket);
        exit(EXIT_FAILURE);
    }

    // Obtencion de update_interval para saber cuando obtener las metricas de nuevo
    if (recv(metrics_socket, &update_interval, sizeof(update_interval), 0) <= 0) {
        perror("No se pudo obtener update_interval");
        close(metrics_socket);
        exit(EXIT_FAILURE);
    }

    printf("Update_interval obtenido del servidor: %d segundos\n", update_interval);

    while (1) {
        // Se utiliza un mutex para obtener las metricas de un cliente
        pthread_mutex_lock(&metrics_mutex);
        get_client_metrics(metrics_buffer, client_name);
        pthread_mutex_unlock(&metrics_mutex);
        //Envio por medio del socket al server
        send(metrics_socket, metrics_buffer, strlen(metrics_buffer), 0);
        sleep(update_interval);
    }

    close(metrics_socket);
}

int main() {
    //uname usado para obtener el nombre del sistema del cliente
    struct utsname uname_data;
    if (uname(&uname_data) != 0) {
        perror("uname error");
        exit(EXIT_FAILURE);
    }

    char hostname[BUFFER_SIZE];
    snprintf(hostname, sizeof(hostname), "%s", uname_data.nodename);

    char client_ip[BUFFER_SIZE];
    get_client_ip(client_ip);

    //Union del nombre con la ip para generar un identificador unico para cada cliente en caso de haber repeticion de nombre
    char client_name[BUFFER_SIZE];
    snprintf(client_name, sizeof(client_name), "%s-%s", hostname, client_ip);

    printf("Enviando metricas desde el cliente %s ...\n", client_name);

    send_metrics(SERVER_IP, SERVER_PORT, client_name);

    return 0;
}
