#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>

#define STRESS_DURATION 30 //Duracion de la prueba de estres

int main() {
    pid_t cpu_pid, load_pid;

    printf("Iniciando la prueba de estres para CPU Usage y Load1, durara %d segundos...\n", STRESS_DURATION);

    // Fork utilizado para ejecutar el estres de CPU Usage
    cpu_pid = fork();
    if (cpu_pid < 0) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    } else if (cpu_pid == 0) {
        //Uso de exec para ejecutar los comandos necesarios para la prueba de estres de la metrica CPU Usage
        execlp("/bin/sh", "/bin/sh", "-c", "while :; do :; done", NULL);
        perror("Exec failed");
        exit(EXIT_FAILURE);
    }

    // Fork utilizado para ejecutar el estres de Load1
    load_pid = fork();
    if (load_pid < 0) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    } else if (load_pid == 0) {
        //Uso de exec para ejecutar los comandos necesarios para la prueba de estres de la metrica Load
        execlp("/bin/sh", "/bin/sh", "-c", "while :; do sleep 0.1 & done", NULL);
        perror("Exec");
        exit(EXIT_FAILURE);
    }

    //Espera a que se cumpla el tiempo de la prueba de estres
    sleep(STRESS_DURATION);

    //Terminar los procesos para que pare la prueba de estres
    if (kill(cpu_pid, SIGKILL) == 0) {
        printf("CPU stress completado.\n");
    } else {
        perror("Fallo al terminar la prueba de estres del CPU");
    }
    if (kill(load_pid, SIGKILL) == 0) {
        printf("Load1 stress completado.\n");
    } else {
        perror("Fallo al terminar la prueba de estres de Load");
    }

    wait(NULL);
    wait(NULL);

    return 0;
}
